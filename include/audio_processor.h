#ifndef AUDIO_PROCESSOR_H
#define AUDIO_PROCESSOR_H

#include "include/data_structures/audio_configuration.h"
#include "include/data_structures/visualization_data.h"
#include <QObject>
#include <QVector>
#include <QAudioFormat>
#include <QMutex>
#include <QTimer>
#include <QThread>
#include <QAtomicInt>
#include <QtMath>
#include <memory>
#include <fftw3.h>
#include <array>
#include <chrono>

// Buffer lock-free para audio en tiempo real
template<typename T, size_t Size>
class LockFreeRingBuffer {
public:
    LockFreeRingBuffer() : m_readPos(0), m_writePos(0) {}

    bool push(const T* data, size_t count) {
        const size_t currentWrite = m_writePos.load(std::memory_order_acquire);
        const size_t currentRead = m_readPos.load(std::memory_order_acquire);

        // Calcular espacio disponible
        const size_t available = (currentRead <= currentWrite)
            ? Size - (currentWrite - currentRead) - 1
            : currentRead - currentWrite - 1;

        if (count > available) {
            return false; // Buffer lleno
        }

        // Escribir datos
        for (size_t i = 0; i < count; ++i) {
            m_buffer[(currentWrite + i) % Size] = data[i];
        }

        m_writePos.store((currentWrite + count) % Size, std::memory_order_release);
        return true;
    }

    size_t pop(T* data, size_t maxCount) {
        const size_t currentRead = m_readPos.load(std::memory_order_acquire);
        const size_t currentWrite = m_writePos.load(std::memory_order_acquire);

        // Calcular datos disponibles
        const size_t available = (currentWrite >= currentRead)
            ? currentWrite - currentRead
            : Size - (currentRead - currentWrite);

        const size_t toCopy = std::min(maxCount, available);

        // Leer datos
        for (size_t i = 0; i < toCopy; ++i) {
            data[i] = m_buffer[(currentRead + i) % Size];
        }

        m_readPos.store((currentRead + toCopy) % Size, std::memory_order_release);
        return toCopy;
    }

    size_t available() const {
        const size_t currentRead = m_readPos.load(std::memory_order_acquire);
        const size_t currentWrite = m_writePos.load(std::memory_order_acquire);

        return (currentWrite >= currentRead)
            ? currentWrite - currentRead
            : Size - (currentRead - currentWrite);
    }

    void clear() {
        m_readPos.store(0, std::memory_order_release);
        m_writePos.store(0, std::memory_order_release);
    }

private:
    std::array<T, Size> m_buffer;
    std::atomic<size_t> m_readPos;
    std::atomic<size_t> m_writePos;
};

// Procesador FFT optimizado con pooling de memoria
class FFTProcessor {
public:
    enum WindowType {
        Rectangle = 0,
        Hanning = 1,
        Hamming = 2,
        Blackman = 3
    };

    FFTProcessor(int fftSize = 1024, WindowType windowType = Hanning);
    ~FFTProcessor();

    // Configuración
    bool setFFTSize(int size);
    void setWindowType(WindowType type);

    // Procesamiento principal (thread-safe)
    bool processFFT(const float* input, float* magnitudeOutput, float* phaseOutput = nullptr);

    // Getters
    int getFFTSize() const { return m_fftSize; }
    WindowType getWindowType() const { return m_windowType; }

    // Utilidades estáticas
    static void convertToDecibels(float* data, int size, float minDb = -80.0f);
    static void applyWindow(float* data, const float* window, int size);

private:
    void generateWindow();
    bool initializeFFT();
    void cleanupFFT();

    int m_fftSize;
    WindowType m_windowType;

    // Buffers alineados para SIMD
    alignas(32) float* m_windowBuffer;
    alignas(32) float* m_inputBuffer;
    alignas(32) fftwf_complex* m_outputBuffer;

    fftwf_plan m_plan;
    QMutex m_fftMutex; // Solo para reconfiguración

    // Pool de memoria para evitar allocaciones
    static constexpr int MAX_FFT_SIZE = 8192;
};

// Analizador de niveles en tiempo real
class LevelAnalyzer {
public:
    LevelAnalyzer(int sampleRate = 44100);

    void processSamples(const float* samples, int count);

    float getPeakLevel() const { return m_peakLevel.load(); }
    float getRMSLevel() const { return m_rmsLevel.load(); }
    float getVULevel() const { return m_vuLevel.load(); }

    void reset();

private:
    std::atomic<float> m_peakLevel{0.0f};
    std::atomic<float> m_rmsLevel{0.0f};
    std::atomic<float> m_vuLevel{0.0f};

    float m_peakDecay;
    float m_rmsDecay;
    float m_vuDecay;

    std::chrono::steady_clock::time_point m_lastUpdate;
};

// Procesador principal optimizado para tiempo real
class AudioProcessor : public QObject {
    Q_OBJECT

public:
    explicit AudioProcessor(const AudioConfiguration& config,
                                   QObject* parent = nullptr);
    ~AudioProcessor();

    // Interfaz principal (thread-safe)
    bool pushAudioData(const float* samples, int count, const QAudioFormat& format);
    bool getVisualizationData(VisualizationData& vizData);

    // Configuración
    void setConfiguration(const AudioConfiguration& config);
    void setUpdateRate(int rateMs);
    void setFFTSize(int size);
    void setWindowType(FFTProcessor::WindowType type);

    // Control
    void start();
    void stop();
    void reset();

    // Estado
    bool isRunning() const { return m_isRunning.load(); }
    int getSampleRate() const { return m_sampleRate.load(); }
    int getChannels() const { return m_channels.load(); }

    // Estadísticas de rendimiento
    struct PerformanceStats {
        float processingLoadPercent;
        int droppedFrames;
        int bufferUnderruns;
        float averageLatencyMs;
    };

    PerformanceStats getPerformanceStats() const;

signals:
    void dataReady(const VisualizationData& data);
    void processingError(const QString& error);
    void performanceWarning(const QString& warning);

private slots:
    void processAudioBuffer();

private:
    // Configuración
    AudioConfiguration m_config;

    // Componentes de procesamiento
    std::unique_ptr<FFTProcessor> m_fftProcessor;
    std::unique_ptr<LevelAnalyzer> m_levelAnalyzer;

    // Buffers lock-free
   static constexpr size_t AUDIO_BUFFER_SIZE = 262144;
    LockFreeRingBuffer<float, AUDIO_BUFFER_SIZE> m_audioBuffer;

    // Buffers de trabajo (pre-alocados)
    std::vector<float> m_workBuffer;
    std::vector<float> m_fftInput;
    std::vector<float> m_magnitudeOutput;
    std::vector<float> m_waveformBuffer;

    // Datos de visualización con double buffering
    struct VisualizationBuffer {
        VisualizationData data;
        std::atomic<bool> ready{false};
        QMutex mutex;
    };

    std::array<VisualizationBuffer, 2> m_vizBuffers;
    std::atomic<int> m_currentWriteBuffer{0};
    std::atomic<int> m_currentReadBuffer{1};

    // Espectrograma circular
    std::vector<std::vector<float>> m_spectrogramRingBuffer;
    std::atomic<int> m_spectrogramWritePos{0};

    // Control de hilos
    QTimer* m_processingTimer;
    QThread* m_processingThread;
    std::atomic<bool> m_isRunning{false};

    // Estado de audio
    std::atomic<int> m_sampleRate{44100};
    std::atomic<int> m_channels{1};

    // Estadísticas de rendimiento
    mutable QMutex m_statsMutex;
    PerformanceStats m_stats;
    std::chrono::steady_clock::time_point m_lastProcessTime;
    int m_processedFrames{0};

    // Suavizado de espectro
    std::vector<float> m_previousSpectrum;
    float m_smoothingFactor{0.7f};

    // Métodos privados
    void initializeProcessing();
    void cleanupProcessing();

    bool processFFTData(const float* samples, int count);
    void updateSpectrogram(const std::vector<float>& spectrum);
    void updateWaveform(const float* samples, int count);
    void smoothSpectrum(std::vector<float>& spectrum);

    void updatePerformanceStats();
    void switchVisualizationBuffers();

    // Optimizaciones SIMD
    void processLevelsVector(const float* samples, int count);
    void downsampleWaveform(const float* input, int inputSize,
                           float* output, int outputSize);
};

// Configuración optimizada para diferentes casos de uso
struct RealtimeConfiguration {
    enum ProcessingMode {
        LowLatency,     // < 10ms latencia, FFT pequeña
        Balanced,       // Balance entre latencia y resolución
        HighResolution  // Máxima resolución espectral
    };

    static AudioConfiguration createConfiguration(ProcessingMode mode,
                                                int sampleRate = 44100) {
        AudioConfiguration config;

        switch (mode) {
        case LowLatency:
            config.updateRateMs = 10;
            config.fftSize = 512;
            config.overlap = 256;
            config.spectrogramHistory = 50;
            config.waveformBufferSize = 2048;
            break;

        case Balanced:
            config.updateRateMs = 25;
            config.fftSize = 1024;
            config.overlap = 768;
            config.spectrogramHistory = 100;
            config.waveformBufferSize = 4096;
            break;

        case HighResolution:
            config.updateRateMs = 50;
            config.fftSize = 4096;
            config.overlap = 3072;
            config.spectrogramHistory = 200;
            config.waveformBufferSize = 8192;
            break;
        }

        return config;
    }
};

#endif // REALTIME_AUDIO_PROCESSOR_H
