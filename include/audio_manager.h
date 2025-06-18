#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QMutex>
#include <QAudioFormat>
#include "include/data_structures/visualization_data.h"
#include "source_controller.h"

class AudioManager : public QObject
{
    Q_OBJECT

public:
    explicit AudioManager(SourceController* controller, QObject* parent = nullptr);
    ~AudioManager();

    // Control principal
    void startProcessing();
    void stopProcessing();
    bool isProcessing() const;

    // Configuración
    void setUpdateRate(int ms);
    void setWaveformDuration(double seconds);
    void setWaveformSamples(int samples);

    // Estadísticas
    double getProcessingLoad() const;
    qint64 getTotalSamplesProcessed() const;
    double getAverageLatency() const;
    QAudioFormat getCurrentFormat() const;

signals:
    void processingStarted();
    void processingStopped();
    void visualizationDataReady(const VisualizationData& data);
    void statisticsUpdated(double loadPercent, qint64 totalSamples, double avgLatencyMs);
    void formatChanged(const QAudioFormat& format);

private slots:
    void handleRawData(SourceType type, const QString& id, const QByteArray& rawData);
    void processAccumulatedData();
    void onFormatDetected(SourceType type, const QString& id, const QAudioFormat& format);
    void onSourceStateChanged(SourceType type, const QString& id, bool active);

private:
    void resetStatistics();
    void updateStatistics(qint64 processingTimeUs, int samplesProcessed);
    VisualizationData processAudioChunk(const QByteArray& data);
    QVector<float> convertToFloat(const QByteArray& data, const QAudioFormat& format);
    QVector<float> calculateSpectrum(const QVector<float>& samples);
    void calculateLevels(const QVector<float>& samples, double& peak, double& rms);

    // Componentes
    SourceController* m_controller;
    QTimer m_processingTimer;

    // Estado
    bool m_isProcessing = false;
    QAudioFormat m_currentFormat;
    bool m_formatValid = false;

    // Configuración
    int m_updateRateMs = 50;           // 20 FPS por defecto
    double m_waveformDurationS = 0.1;  // 100ms de historia
    int m_waveformSamples = 1024;      // Puntos para visualización

    // Buffer de datos
    QMutex m_dataMutex;
    QByteArray m_pendingData;
    int m_maxBufferSize = 1024 * 1024; // 1MB máximo

    // Estadísticas
    qint64 m_totalProcTimeUs = 0;
    qint64 m_totalSamples = 0;
    int m_chunksCount = 0;
    double m_avgLatencyMs = 0.0;
};

#endif // AUDIO_MANAGER_H
