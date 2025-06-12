#include <QObject>
#include <QAudioDecoder>
#include <QThread>
#include <QVector>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>

// Lightweight FIFO for PCM samples (mono or de‑interleaved)
class SampleRingBuffer {
public:
    explicit SampleRingBuffer(qint64 capacity = 10'000'000)
        : m_buffer(capacity), m_head(0), m_tail(0), m_size(0) {}

    void push(const float *data, qint64 frames) {
        QMutexLocker lock(&m_mutex);
        for (qint64 i = 0; i < frames; ++i) {
            m_buffer[m_head] = data[i];
            m_head = (m_head + 1) % m_buffer.size();
            if (m_size < m_buffer.size()) ++m_size; else m_tail = (m_tail + 1) % m_buffer.size();
        }
        m_notEmpty.wakeAll();
    }

    // Blocking pop — waits until at least n frames are available.
    qint64 pop(float *dst, qint64 frames) {
        QMutexLocker lock(&m_mutex);
        while (m_size < frames)
            m_notEmpty.wait(&m_mutex);
        for (qint64 i = 0; i < frames; ++i) {
            dst[i] = m_buffer[m_tail];
            m_tail = (m_tail + 1) % m_buffer.size();
        }
        m_size -= frames;
        return frames;
    }

    qint64 size() const { return m_size; }

private:
    QVector<float>   m_buffer;
    qint64           m_head;
    qint64           m_tail;
    qint64           m_size;
    mutable QMutex   m_mutex;
    QWaitCondition   m_notEmpty;
};

// ----------------------------------------------------------------
// AudioEngine: orchestrates decode + DSP workers and exposes results
// ----------------------------------------------------------------
class AudioEngine : public QObject {
    Q_OBJECT
public:
    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine() override;

    // Opens/decodes the given file. Returns false if it cannot be started.
    bool openFile(const QString &filePath);

    // Waveform request: down‑samples PCM into 'samplesPerPixel' bins in [startFrame, start+frames)
    // Returns vector with min/max interleaved (2 * pixels) for fast GL rendering.
    QVector<float> requestWaveform(qint64 startFrame, qint64 frames, int samplesPerPixel);

signals:
    // Emitted progressively when a spectrogram tile is ready (time‑index, RGB32 image)
    void spectrogramTileReady(int tileIndex, const QImage &tile);

    // Emitted when the decoder has parsed the basic stream info (sampleRate, totalFrames, etc.)
    void streamInfoAvailable(int sampleRate, qint64 totalFrames);

private slots:
    void handleBufferReady();
    void handleFinished();

private:
    void startDSPThread();
    void stopDSPThread();

    // ---------- members ----------
    QAudioDecoder     m_decoder;
    int               m_sampleRate   = 0;
    int               m_channels     = 0;
    qint64            m_totalFrames  = 0;

    SampleRingBuffer  m_ring;              // raw mono PCM for analysis

    // DSP thread infrastructure
    QThread          *m_dspThread     = nullptr;
    bool              m_abortDSP      = false;
};
