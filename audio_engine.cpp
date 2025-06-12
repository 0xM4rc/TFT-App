#include "include/audio_engine.h"
#include <QThread>
#include <QDebug>
#include <QUrl>

AudioEngine::AudioEngine(QObject *parent) : QObject(parent) {
    connect(&m_decoder, &QAudioDecoder::bufferReady, this, &AudioEngine::handleBufferReady);
    connect(&m_decoder, &QAudioDecoder::finished,      this, &AudioEngine::handleFinished);
}

AudioEngine::~AudioEngine() {
    stopDSPThread();
}

bool AudioEngine::openFile(const QString &filePath) {
    if (m_decoder.isDecoding())
        m_decoder.stop();

    m_decoder.setSource(QUrl::fromLocalFile(filePath));
    m_decoder.start();
    if (m_decoder.error() != QAudioDecoder::NoError) {
        qWarning() << "AudioEngine: cannot start decoder:" << m_decoder.errorString();
        return false;
    }
    return true;
}

void AudioEngine::handleBufferReady() {
    auto buffer = m_decoder.read();
    if (!buffer.isValid()) return;

    // Lazily capture stream info on first buffer
    if (m_sampleRate == 0) {
        auto fmt = buffer.format();
        m_sampleRate = fmt.sampleRate();
        m_channels   = fmt.channelCount();
        // Remove unused variable warning
        // qint64 bytesPerFrame = fmt.bytesPerFrame();
        // totalFrames gets refined when finished()
        emit streamInfoAvailable(m_sampleRate, 0);
    }

    // Convert to float mono
    const qint16 *src16 = buffer.constData<qint16>();
    int frames = buffer.frameCount();
    static thread_local QVector<float> tmp(8192);
    tmp.resize(frames);
    for (int i = 0; i < frames; ++i) {
        int ch = i * m_channels; // Take first channel as mono
        tmp[i] = src16[ch] / 32768.f;
    }
    m_ring.push(tmp.constData(), frames);
}

void AudioEngine::handleFinished() {
    m_totalFrames = m_decoder.position() * m_sampleRate / 1000; // approx
    emit streamInfoAvailable(m_sampleRate, m_totalFrames);
    startDSPThread();
}

QVector<float> AudioEngine::requestWaveform(qint64 startFrame, qint64 frames, int spp) {
    if (spp <= 0) return {};
    QVector<float> dst;
    dst.resize((frames / spp) * 2); // min/max pairs

    qint64 idx = 0;
    for (qint64 f = 0; f < frames; f += spp) {
        float mn =  1.f, mx = -1.f;
        for (int s = 0; s < spp; ++s) {
            qint64 pos = startFrame + f + s;
            if (pos >= m_ring.size()) break; // no data yet
            mn = std::min(mn, m_ring.size() ? 0.f : 0.f); // placeholder
            mx = std::max(mx, m_ring.size() ? 0.f : 0.f);
        }
        dst[idx++] = mn;
        dst[idx++] = mx;
    }
    return dst;
}

// -------------------- DSP thread (spectrogram) ------------------
static QImage tileFromFFT(const QVector<float> &mag, int width, int height) {
    Q_UNUSED(mag) // Suppress unused parameter warning
    QImage img(width, height, QImage::Format_RGB32);
    img.fill(Qt::black);
    // TODO: map magnitudes to grayscale or colormap
    return img;
}

void AudioEngine::startDSPThread() {
    stopDSPThread();
    m_abortDSP  = false;
    m_dspThread = QThread::create([this](){
        const int fftSize = 4096;
        const int hop     = fftSize / 4;

        QVector<float> window(fftSize);
        for (int i = 0; i < fftSize; ++i)
            window[i] = 0.5f * (1.f - cosf(2.f*M_PI*i/(fftSize-1))); // Hann

        qint64 framePos = 0;
        QVector<float> pcm(fftSize);
        int tileIdx = 0;
        while (!m_abortDSP) {
            if (m_ring.size() - framePos < fftSize) {
                QThread::msleep(5);
                continue;
            }
            // Fetch data (blocking pop)
            m_ring.pop(pcm.data(), fftSize);

            // Apply window
            QVector<float> re(fftSize), im(fftSize);
            for (int i = 0; i < fftSize; ++i)
                re[i] = pcm[i] * window[i];
            // TODO: FFT using KissFFT
            // for now, generate dummy magnitude
            QVector<float> mag(fftSize/2);
            for (int i = 0; i < mag.size(); ++i) mag[i] = fabsf(re[i]);

            auto img = tileFromFFT(mag, 512, 256);
            emit spectrogramTileReady(tileIdx++, img);

            framePos += hop;
        }
    });
    m_dspThread->start();
}

void AudioEngine::stopDSPThread() {
    if (!m_dspThread) return;
    m_abortDSP = true;
    m_dspThread->quit();
    m_dspThread->wait();
    delete m_dspThread;
    m_dspThread = nullptr;
}
