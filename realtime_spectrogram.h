#ifndef REALTIME_SPECTROGRAM_H
#define REALTIME_SPECTROGRAM_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include <QCache>
#include <QTimer>
#include <complex>
#include <memory>

struct SpectrogramFrame {
    qint64 blockIndex;
    qint64 timestamp;
    QVector<float> magnitudes;  // Magnitudes FFT
    QVector<float> frequencies; // Frecuencias correspondientes
    float sampleRate;
    int fftSize;
};

struct SpectrogramConfig {
    int fftSize = 2048;           // Tamaño FFT
    int overlap = 1024;           // Solapamiento entre ventanas
    int sampleRate = 44100;       // Frecuencia de muestreo
    float windowType = 0;         // 0=Hann, 1=Hamming, 2=Blackman
    int cacheSize = 1000;         // Máximo frames en caché
    bool enableSmoothing = true;  // Suavizado temporal
    float smoothingFactor = 0.8f; // Factor de suavizado (0-1)
};

class RealtimeSpectrogram : public QObject
{
    Q_OBJECT

public:
    explicit RealtimeSpectrogram(const SpectrogramConfig& config = SpectrogramConfig(),
                                 QObject* parent = nullptr);
    ~RealtimeSpectrogram();

    // Configuración
    void setConfig(const SpectrogramConfig& config);
    SpectrogramConfig getConfig() const { return m_config; }

    // Procesamiento de bloques
    bool processAudioBlock(qint64 blockIndex, const QByteArray& audioData,
                           qint64 timestamp = 0);

    // Obtener datos del espectrograma
    QVector<SpectrogramFrame> getFramesInRange(qint64 startTime, qint64 endTime) const;
    SpectrogramFrame getLatestFrame() const;
    QVector<SpectrogramFrame> getAllFrames() const;

    // Obtener espectrograma completo (matriz de magnitudes)
    QVector<QVector<float>> getSpectrogramMatrix(qint64 startTime = 0, qint64 endTime = -1) const;

    // Utilidades
    QVector<float> getFrequencyAxis() const;
    void clearCache();
    int getCachedFrameCount() const;

    // Estadísticas
    QString getStatistics() const;

signals:
    void frameReady(const SpectrogramFrame& frame);
    void errorOccurred(const QString& error);
    void cacheUpdated(int frameCount);

private slots:
    void performMaintenance();

private:
    // Configuración
    SpectrogramConfig m_config;
    mutable QMutex m_mutex;

    // Cache de frames
    QCache<qint64, SpectrogramFrame> m_frameCache;
    QVector<qint64> m_frameOrder; // Para mantener orden temporal

    // Procesamiento FFT
    QVector<std::complex<float>> m_fftBuffer;
    QVector<float> m_window;
    QVector<float> m_previousFrame; // Para suavizado temporal

    // Buffer de solapamiento
    QVector<float> m_overlapBuffer;
    bool m_hasOverlap;

    // Timer para mantenimiento
    QTimer* m_maintenanceTimer;

    // Métodos privados
    void initializeFFT();
    void generateWindow();
    QVector<float> computeFFT(const QVector<float>& samples);
    QVector<float> convertToFloat(const QByteArray& audioData) const;
    void applyWindow(QVector<float>& samples);
    QVector<float> calculateMagnitudes(const QVector<std::complex<float>>& fftResult);
    void applySmoothingFilter(QVector<float>& magnitudes);
    void addFrameToCache(const SpectrogramFrame& frame);
    void cleanupOldFrames();

    // FFT Implementation (simple DFT for demonstration)
    void performFFT(QVector<std::complex<float>>& data);
    void bitReverse(QVector<std::complex<float>>& data);
};

#endif // REALTIME_SPECTROGRAM_H
