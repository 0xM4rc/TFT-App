#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include "include/data_structures/audio_configuration.h"
#include "include/data_structures/visualization_data.h"
#include <QObject>
#include <QVector>
#include <QAudioFormat>
#include <QMutex>
#include <QtMath>
#include <memory>
#include <fftw3.h>

// Buffer circular para audio
class AudioBuffer {
public:
    AudioBuffer(int maxSize = 8192);

    void setMaxSize(int size);
    void append(const QVector<float>& data);
    void append(const float* data, int count);
    QVector<float> getLastSamples(int count) const;
    QVector<float> getAllSamples() const;
    void clear();

    int size() const { return m_currentSize; }
    int maxSize() const { return m_maxSize; }
    bool isEmpty() const { return m_currentSize == 0; }
    bool isFull() const { return m_currentSize >= m_maxSize; }

private:
    QVector<float> m_buffer;
    int m_writePos;
    int m_currentSize;
    int m_maxSize;
    mutable QMutex m_mutex;
};

// Procesador FFT usando FFTW
class FFTProcessor {
public:
    FFTProcessor(int fftSize = DEFAULT_FFT_SIZE);
    ~FFTProcessor();

    void setFFTSize(int size);
    int getFFTSize() const { return m_fftSize; }

    // Procesar FFT real → complejo
    bool processFFT(const QVector<float>& input, QVector<float>& output);

    // Aplicar ventana a los datos
    void applyWindow(QVector<float>& data);

    // Convertir magnitud a dB
    static void convertToDecibels(QVector<float>& data, float minDb = -100.0f);

    enum WindowType {
        Rectangle,
        Hanning,
        Hamming,
        Blackman
    };

    void setWindowType(WindowType type);
    WindowType getWindowType() const { return m_windowType; }

private:
    void generateWindow();
    void initializeFFT();
    void cleanupFFT();

    int                    m_fftSize;
    WindowType             m_windowType;
    QVector<float>         m_window;

    // FFTW structures
    float*                 m_inputRealBuffer = nullptr;
    fftwf_complex*         m_fftBuffer      = nullptr;
    fftwf_plan             m_plan;

    // workspace for optional processing
    QVector<float>         m_workBuffer;

    static const int       DEFAULT_FFT_SIZE = 1024;
};

// Procesador principal de audio
class AudioProcessor : public QObject {
    Q_OBJECT

public:
    explicit AudioProcessor(const AudioConfiguration& config,
                            QObject* parent = nullptr);

    // Configuración FFT
    void setFFTSize(int size);
    int getFFTSize() const;
    void setOverlap(int samples);
    int getOverlap() const;
    void setSpectrogramHistory(int frames);
    int getSpectrogramHistory() const;
    void setWindowType(FFTProcessor::WindowType type);
    FFTProcessor::WindowType getWindowType() const;

    void setWaveformBufferSize(int samples);
    int getWaveformBufferSize() const;

    // Procesamiento principal
    bool processAudioData(const QVector<float>& samples,
                          const QAudioFormat& format,
                          VisualizationData& vizData);

    // Control y estado
    void reset();
    void clearHistory();
    bool isReady() const;
    bool isEnabled() const { return m_enabled; }

    // Configuración de rango de frecuencias
    void setFrequencyRange(float minFreq, float maxFreq);
    void getFrequencyRange(float& minFreq, float& maxFreq) const;

     void applyConfiguration(const AudioConfiguration& config);

signals:
    void processingError(const QString& error);

public slots:
    void setEnabled(bool enabled) { m_enabled = enabled; }

private slots:
    void handleProcessingError(const QString& error);

private:
    AudioConfiguration  m_config;
    // Métodos internos
    void processWaveform(VisualizationData& vizData);
    void processSpectrum(const QVector<float>& samples, VisualizationData& vizData);
    void updateSpectrogram(const QVector<float>& spectrum);
    void ensureInitialized(const QAudioFormat& format);
    QVector<float> resampleForVisualization(const QVector<float>& input, int targetSize);
    void smoothSpectrum(QVector<float>& spectrum, float smoothingFactor = 0.8f);

    // Estado
    bool                    m_isInitialized = false;
    bool                    m_enabled       = true;
    QAudioFormat            m_currentFormat;

    // Componentes
    std::unique_ptr<AudioBuffer>  m_waveformBuffer;
    std::unique_ptr<FFTProcessor> m_fftProcessor;

    // Configuración
    int                     m_fftSize;
    int                     m_overlap;
    int                     m_spectrogramHistory;
    int                     m_waveformBufferSize;
    float                   m_minFrequency;
    float                   m_maxFrequency;

    // Datos del espectrograma
    QVector<QVector<float>> m_spectrogramData;
    QVector<float>         m_previousSpectrum;

    // Buffer de muestras para FFT
    QVector<float>         m_processingBuffer;
    int                     m_samplesProcessed;

    mutable QMutex          m_processingMutex;

    // Constantes por defecto
    static const int       DEFAULT_OVERLAP            = 512;
    static const int       DEFAULT_SPECTROGRAM_HISTORY= 200;
    static const int       DEFAULT_WAVEFORM_BUFFER_SIZE = 8192;
    static const float     DEFAULT_MIN_FREQUENCY;
    static const float     DEFAULT_MAX_FREQUENCY;
};

Q_DECLARE_METATYPE(VisualizationData)

#endif // AUDIO_PROCESSOR_H
