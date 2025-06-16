#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include "include/data_structures/visualization_data.h"
#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <memory>

// Forward declarations
class AudioSource;
class AudioProcessor;


class AudioManager : public QObject
{
    Q_OBJECT

public:
    explicit AudioManager(QObject *parent = nullptr);
    ~AudioManager();

    // Control principal
    void setAudioSource(AudioSource* source);
    void startProcessing();
    void stopProcessing();
    bool isProcessing() const;

    // Configuración
    void setUpdateRate(int milliseconds);           // Tasa de actualización de visualización
    void setFFTSize(int size);                      // Tamaño de FFT (potencia de 2)
    void setWaveformDuration(double seconds);       // Duración visible del waveform
    void setProcessingEnabled(bool enabled);        // Habilitar/deshabilitar procesamiento DSP

    // Getters
    int updateRate() const { return m_updateRate; }
    int fftSize() const { return m_fftSize; }
    double waveformDuration() const { return m_waveformDuration; }
    QAudioFormat currentFormat() const;
    QString currentSourceName() const;

    // Estadísticas
    double getProcessingLoad() const;               // % de CPU usado en procesamiento
    qint64 getTotalSamplesProcessed() const;
    double getAverageLatency() const;

signals:
    // Señal principal para la GUI
    void visualizationDataReady(const VisualizationData& data);

    // Señales de estado
    void processingStarted();
    void processingStopped();
    void formatChanged(const QAudioFormat& format);
    void sourceChanged(const QString& sourceName);

    // Señales de error y estadísticas
    void error(const QString& message);
    void statisticsUpdated(double cpuLoad, qint64 totalSamples, double avgLatency);

private slots:
    void processAudioData();
    void handleSourceDataReady();
    void handleSourceStateChanged(bool active);
    void handleSourceError(const QString& error);
    void handleSourceFormatDetected(const QAudioFormat& format);

private:
    // Métodos internos
    void connectSource();
    void disconnectSource();
    void updateStatistics();
    void resetStatistics();
    QVector<float> convertBytesToFloat(const QByteArray& data, const QAudioFormat& format);
    void calculateLevels(const QVector<float>& samples, double& peak, double& rms);

    // Miembros principales
    AudioSource* m_audioSource;
    std::unique_ptr<AudioProcessor> m_processor;
    QTimer* m_processingTimer;

    // Configuración
    int m_updateRate;           // ms entre actualizaciones
    int m_fftSize;              // Tamaño FFT
    double m_waveformDuration;  // segundos
    bool m_processingEnabled;

    // Estado
    bool m_isProcessing;
    QAudioFormat m_currentFormat;
    QString m_currentSourceName;

    // Buffer y sincronización
    QMutex m_dataMutex;
    QByteArray m_pendingData;

    // Estadísticas y rendimiento
    QElapsedTimer m_processingTimer_stats;
    qint64 m_totalProcessingTime;       // microsegundos
    qint64 m_totalSamplesProcessed;
    int m_processedChunks;
    double m_averageLatency;

    // Constantes
    static const int DEFAULT_UPDATE_RATE = 33;      // ~30 FPS
    static const int DEFAULT_FFT_SIZE = 1024;
    static const double DEFAULT_WAVEFORM_DURATION;  // 2.0 seconds
};

#endif // AUDIO_MANAGER_H
