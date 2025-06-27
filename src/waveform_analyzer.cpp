#include "include/waveform_analyzer.h"
#include <QtMath>
#include <QDateTime>
#include <algorithm>

WaveformAnalyzer::WaveformAnalyzer(QObject* parent)
    : QObject(parent)
    , m_windowSize(1024)
    , m_hopSize(256)
    , m_smoothingFactor(0.1f)
    , m_gainCompensation(1.0f)
    , m_maxHistorySize(48000)
    , m_downsampleRatio(4)
    , m_downsampleCounter(0)
    , m_adaptiveGain(1.0f)
    , m_peakHoldTime(500)
    , m_lastEnvelopeValue(0.0f)
    , m_startTime(QDateTime::currentMSecsSinceEpoch()) // 500ms de hold para picos
{
    m_processingBuffer.reserve(m_windowSize * 2);

    // Historial multi-resolución
    m_detailHistory.reserve(m_maxHistorySize);      // Resolución completa
    m_mediumHistory.reserve(m_maxHistorySize / 10); // 1:10 downsampling
    m_overviewHistory.reserve(m_maxHistorySize / 60); // 1:60 downsampling

    // Buffer para acumulación de downsampling
    m_downsampleAccumulator = WaveformData();
    m_mediumAccumulator = WaveformData();

    // Detección de picos
    m_peakHoldValue = 0.0f;
    m_peakHoldTimer.start();
}

WaveformAnalyzer::~WaveformAnalyzer() = default;

void WaveformAnalyzer::setAnalysisParams(int windowSize, int hopSize)
{
    m_windowSize = windowSize;
    m_hopSize = hopSize;
    m_processingBuffer.reserve(windowSize * 2);
}

void WaveformAnalyzer::setHistorySize(int seconds, int sampleRate)
{
    int blocksPerSecond = sampleRate / m_hopSize;
    m_maxHistorySize = seconds * blocksPerSecond;

    QMutexLocker locker(&m_historyMutex);

    // Redimensionar todos los historiales
    m_detailHistory.reserve(m_maxHistorySize);
    m_mediumHistory.reserve(m_maxHistorySize / 10);
    m_overviewHistory.reserve(m_maxHistorySize / 60);

    // Recortar si es necesario
    trimHistory(m_detailHistory, m_maxHistorySize);
    trimHistory(m_mediumHistory, m_maxHistorySize / 10);
    trimHistory(m_overviewHistory, m_maxHistorySize / 60);
}

void WaveformAnalyzer::setDownsampleRatio(int ratio)
{
    m_downsampleRatio = qMax(1, ratio);
    m_downsampleCounter = 0;
}

void WaveformAnalyzer::processBlock(const QVector<float>& samples)
{
    if (samples.isEmpty()) return;

    // Normalización adaptativa automática
    updateAdaptiveGain(samples);

    m_processingBuffer.append(samples);

    while (m_processingBuffer.size() >= m_windowSize) {
        QVector<float> window = m_processingBuffer.mid(0, m_windowSize);
        analyzeBlock(window);

        int removeCount = qMin(m_hopSize, m_processingBuffer.size());
        m_processingBuffer.remove(0, removeCount);
    }
}

void WaveformAnalyzer::analyzeBlock(const QVector<float>& samples)
{
    // Calcular métricas básicas con ganancia adaptativa
    float peak = 0.0f;
    float sum = 0.0f;
    float sumSquares = 0.0f;

    for (float sample : samples) {
        float compensated = sample * m_gainCompensation * m_adaptiveGain;
        float absSample = qAbs(compensated);

        peak = qMax(peak, absSample);
        sum += absSample;
        sumSquares += compensated * compensated;
    }

    float rms = qSqrt(sumSquares / samples.size());
    float average = sum / samples.size();

    // Detección y hold de picos
    updatePeakDetection(peak);

    // Smoothing del envelope
    float envelope = peak;
    if (m_lastEnvelopeValue > 0.0f) {
        envelope = m_lastEnvelopeValue * (1.0f - m_smoothingFactor) +
                   peak * m_smoothingFactor;
    }
    m_lastEnvelopeValue = envelope;

    // Crear datos con información de pico
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch() - m_startTime;
    WaveformData data(envelope, rms, average, currentTime);
    data.peakValue = m_peakHoldValue;
    data.isPeak = (peak >= m_peakHoldValue * 0.95f); // Marcar como pico si está cerca del hold

    // Actualizar historiales multi-resolución
    updateMultiResolutionHistory(data);

    // Emitir señales
    emit waveformReady(data);

    // Envelope detallado solo si se solicita
    if (receivers(SIGNAL(envelopeReady(QVector<float>))) > 0) {
        generateDetailedEnvelope(samples);
    }
}

void WaveformAnalyzer::updateAdaptiveGain(const QVector<float>& samples)
{
    // Calcular nivel promedio del bloque
    float blockLevel = 0.0f;
    for (float sample : samples) {
        blockLevel += qAbs(sample);
    }
    blockLevel /= samples.size();

    // Ajustar ganancia adaptativa gradualmente
    const float targetLevel = 0.3f; // Nivel objetivo
    const float adaptRate = 0.001f; // Velocidad de adaptación lenta

    if (blockLevel > 0.001f) { // Evitar división por cero
        float idealGain = targetLevel / blockLevel;
        float maxGainChange = 1.0f + adaptRate;
        float minGainChange = 1.0f - adaptRate;

        // Limitar cambios bruscos
        idealGain = qBound(m_adaptiveGain * minGainChange,
                           idealGain,
                           m_adaptiveGain * maxGainChange);

        // Limitar ganancia total
        m_adaptiveGain = qBound(0.1f, idealGain, 10.0f);
    }
}

void WaveformAnalyzer::updatePeakDetection(float currentPeak)
{
    // Actualizar pico si es mayor
    if (currentPeak > m_peakHoldValue) {
        m_peakHoldValue = currentPeak;
        m_peakHoldTimer.restart();
    }
    // Decay del pico después del tiempo de hold
    else if (m_peakHoldTimer.elapsed() > m_peakHoldTime) {
        float decayRate = 0.999f; // Decay lento
        m_peakHoldValue *= decayRate;

        // Mínimo para evitar que se vaya a cero
        if (m_peakHoldValue < currentPeak) {
            m_peakHoldValue = currentPeak;
        }
    }
}

void WaveformAnalyzer::updateMultiResolutionHistory(const WaveformData& data)
{
    QMutexLocker locker(&m_historyMutex);

    // Historial detallado (resolución completa)
    m_detailHistory.append(data);
    trimHistory(m_detailHistory, m_maxHistorySize);

    // Downsampling para resolución media (1:10)
    accumulateData(m_downsampleAccumulator, data);
    m_downsampleCounter++;

    if (m_downsampleCounter >= m_downsampleRatio) {
        // Promediar datos acumulados
        WaveformData avgData = m_downsampleAccumulator;
        avgData.envelope /= m_downsampleRatio;
        avgData.rms /= m_downsampleRatio;
        avgData.average /= m_downsampleRatio;

        m_mediumHistory.append(avgData);
        trimHistory(m_mediumHistory, m_maxHistorySize / 10);

        // Downsampling para overview (1:60 total)
        accumulateData(m_mediumAccumulator, avgData);
        static int mediumCounter = 0;
        mediumCounter++;

        if (mediumCounter >= 6) { // 10 * 6 = 60
            WaveformData overviewData = m_mediumAccumulator;
            overviewData.envelope /= 6;
            overviewData.rms /= 6;
            overviewData.average /= 6;

            m_overviewHistory.append(overviewData);
            trimHistory(m_overviewHistory, m_maxHistorySize / 60);

            m_mediumAccumulator = WaveformData();
            mediumCounter = 0;
        }

        // Reset acumulador
        m_downsampleAccumulator = WaveformData();
        m_downsampleCounter = 0;
    }

    emit historyUpdated();
}

void WaveformAnalyzer::generateDetailedEnvelope(const QVector<float>& samples)
{
    QVector<float> envelopeData;
    envelopeData.reserve(samples.size());

    for (float sample : samples) {
        float compensated = qAbs(sample * m_gainCompensation * m_adaptiveGain);
        envelopeData.append(compensated);
    }

    emit envelopeReady(envelopeData);
}

void WaveformAnalyzer::accumulateData(WaveformData& accumulator, const WaveformData& newData)
{
    if (accumulator.timestamp == 0) {
        accumulator = newData;
    } else {
        accumulator.envelope += newData.envelope;
        accumulator.rms += newData.rms;
        accumulator.average += newData.average;
        accumulator.timestamp = newData.timestamp; // Usar timestamp más reciente
        accumulator.peakValue = qMax(accumulator.peakValue, newData.peakValue);
        accumulator.isPeak = accumulator.isPeak || newData.isPeak;
    }
}

void WaveformAnalyzer::trimHistory(QVector<WaveformData>& history, int maxSize)
{
    if (history.size() > maxSize) {
        int toRemove = history.size() - maxSize;
        history.remove(0, toRemove);
    }
}

QVector<WaveformData> WaveformAnalyzer::getHistoryData(int seconds, HistoryResolution resolution) const
{
    QMutexLocker locker(&m_historyMutex);  // Ahora funciona porque m_historyMutex es mutable

    const QVector<WaveformData>* sourceHistory;
    switch (resolution) {
    case HistoryResolution::Detail:
        sourceHistory = &m_detailHistory;
        break;
    case HistoryResolution::Medium:
        sourceHistory = &m_mediumHistory;
        break;
    case HistoryResolution::Overview:
        sourceHistory = &m_overviewHistory;
        break;
    default:
        sourceHistory = &m_detailHistory;
    }

    if (seconds <= 0) {
        return *sourceHistory;
    }

    qint64 cutoffTime = QDateTime::currentMSecsSinceEpoch() - m_startTime - (seconds * 1000);

    QVector<WaveformData> filtered;
    for (const auto& data : *sourceHistory) {
        if (data.timestamp >= cutoffTime) {
            filtered.append(data);
        }
    }

    return filtered;
}

QVector<WaveformData> WaveformAnalyzer::getOptimalHistoryData(int seconds, int maxPoints) const
{
    HistoryResolution resolution = HistoryResolution::Detail;

    QMutexLocker locker(&m_historyMutex);  // Ahora funciona
    int estimatedPoints = (seconds * m_maxHistorySize) / 600;

    if (estimatedPoints > maxPoints * 2) {
        resolution = HistoryResolution::Overview;
    } else if (estimatedPoints > maxPoints) {
        resolution = HistoryResolution::Medium;
    }

    locker.unlock();
    return getHistoryData(seconds, resolution);
}


void WaveformAnalyzer::clearHistory()
{
    QMutexLocker locker(&m_historyMutex);

    m_detailHistory.clear();
    m_mediumHistory.clear();
    m_overviewHistory.clear();

    m_downsampleAccumulator = WaveformData();
    m_mediumAccumulator = WaveformData();
    m_downsampleCounter = 0;

    m_startTime = QDateTime::currentMSecsSinceEpoch();
    emit historyUpdated();
}

float WaveformAnalyzer::calculateRMS(const QVector<float>& samples)
{
    if (samples.isEmpty()) return 0.0f;

    float sum = 0.0f;
    for (float sample : samples) {
        float compensated = sample * m_gainCompensation * m_adaptiveGain;
        sum += compensated * compensated;
    }

    return qSqrt(sum / samples.size());
}

float WaveformAnalyzer::calculateAverage(const QVector<float>& samples)
{
    if (samples.isEmpty()) return 0.0f;

    float sum = 0.0f;
    for (float sample : samples) {
        sum += qAbs(sample * m_gainCompensation * m_adaptiveGain);
    }

    return sum / samples.size();
}
