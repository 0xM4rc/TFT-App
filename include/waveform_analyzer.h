#ifndef WAVEFORM_ANALYZER_H
#define WAVEFORM_ANALYZER_H

#include <QObject>
#include <QVector>
#include <QMutex>
#include <QDateTime>

// Helper struct holding envelope/rms/average/peak data per timestamp
struct WaveformData {
    float envelope = 0.0f;
    float rms = 0.0f;
    float average = 0.0f;
    qint64 timestamp = 0;
    float peakValue = 0.0f;
    bool isPeak = false;

    WaveformData() = default;
    WaveformData(float env, float r, float avg, qint64 ts)
        : envelope(env), rms(r), average(avg), timestamp(ts) {}
};

class WaveformAnalyzer : public QObject
{
    Q_OBJECT

public:
    enum class HistoryResolution {
        Detail,
        Medium,
        Overview
    };

    explicit WaveformAnalyzer(QObject* parent = nullptr);
    ~WaveformAnalyzer() override;

    /// Configuration
    void setAnalysisParams(int windowSize, int hopSize);
    void setHistorySize(int seconds, int sampleRate);
    void setDownsampleRatio(int ratio);

public slots:
    /// Feed a block of raw samples into the analyzer
    void processBlock(const QVector<float>& samples);

    /// Control/history
    QVector<WaveformData> getHistoryData(int seconds = 0,
                                         HistoryResolution resolution = HistoryResolution::Detail) const;
    QVector<WaveformData> getOptimalHistoryData(int seconds, int maxPoints) const;
    void clearHistory();

    /// Utility
    float calculateRMS(const QVector<float>& samples);
    float calculateAverage(const QVector<float>& samples);

signals:
    /// Emitted when a new summary data point is ready
    void waveformReady(const WaveformData& data);

    /// Emitted if detailed per-sample envelope is requested
    void envelopeReady(const QVector<float>& envelope);

    /// Emitted whenever any history buffer is updated
    void historyUpdated();

private:
    /// Core processing steps
    void analyzeBlock(const QVector<float>& samples);
    void updateAdaptiveGain(const QVector<float>& samples);
    void updatePeakDetection(float currentPeak);
    void updateMultiResolutionHistory(const WaveformData& data);
    void generateDetailedEnvelope(const QVector<float>& samples);

    /// History management
    void accumulateData(WaveformData& accumulator, const WaveformData& newData);
    void trimHistory(QVector<WaveformData>& history, int maxSize);

    // Analysis parameters
    int   m_windowSize;
    int   m_hopSize;
    float m_smoothingFactor;
    float m_gainCompensation;

    // Multi-resolution history
    int   m_maxHistorySize;
    QVector<WaveformData> m_detailHistory;
    QVector<WaveformData> m_mediumHistory;
    QVector<WaveformData> m_overviewHistory;
    mutable QMutex m_historyMutex;

    // Buffers & accumulators
    QVector<float> m_processingBuffer;
    WaveformData   m_downsampleAccumulator;
    WaveformData   m_mediumAccumulator;
    int            m_downsampleRatio;
    int            m_downsampleCounter;

    // Adaptive gain
    float m_adaptiveGain;

    // Peak hold
    float m_peakHoldValue;
    int   m_peakHoldTime;      // ms
    QElapsedTimer m_peakHoldTimer;

    // Envelope smoothing
    float m_lastEnvelopeValue;

    // Time origin
    qint64 m_startTime;
};

#endif // WAVEFORM_ANALYZER_H
