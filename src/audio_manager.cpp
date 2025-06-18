#include "include/audio_manager.h"
#include "qdatetime.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QtMath>
#include <algorithm>

AudioManager::AudioManager(SourceController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
    // Timer para procesar datos acumulados
    m_processingTimer.setInterval(m_updateRateMs);
    connect(&m_processingTimer, &QTimer::timeout,
            this, &AudioManager::processAccumulatedData);

    // Conectar señales del controlador
    connect(m_controller, &SourceController::dataReady,
            this, &AudioManager::handleRawData);

    connect(m_controller, &SourceController::formatDetected,
            this, &AudioManager::onFormatDetected);

    connect(m_controller, &SourceController::stateChanged,
            this, &AudioManager::onSourceStateChanged);

    qDebug() << "AudioManager initialized with" << m_updateRateMs << "ms update rate";
}

AudioManager::~AudioManager()
{
    stopProcessing();
}

void AudioManager::startProcessing()
{
    if (m_isProcessing) {
        qDebug() << "AudioManager already processing";
        return;
    }

    resetStatistics();

    // Obtener formato actual si está disponible
    m_currentFormat = m_controller->activeFormat();
    m_formatValid = m_currentFormat.isValid();

    if (!m_formatValid) {
        qWarning() << "Starting without valid audio format - will wait for detection";
    }

    m_controller->start();
    m_processingTimer.start();
    m_isProcessing = true;

    emit processingStarted();
    qDebug() << "AudioManager processing started";
}

void AudioManager::stopProcessing()
{
    if (!m_isProcessing) {
        qDebug() << "AudioManager not processing";
        return;
    }

    m_processingTimer.stop();
    m_controller->stop();

    // Limpiar buffer pendiente
    QMutexLocker locker(&m_dataMutex);
    m_pendingData.clear();
    locker.unlock();

    m_isProcessing = false;
    emit processingStopped();
    qDebug() << "AudioManager processing stopped";
}

bool AudioManager::isProcessing() const
{
    return m_isProcessing;
}

void AudioManager::setUpdateRate(int ms)
{
    if (ms < 10 || ms > 1000) {
        qWarning() << "Invalid update rate:" << ms << "ms (valid range: 10-1000)";
        return;
    }

    m_updateRateMs = ms;
    if (m_processingTimer.isActive()) {
        m_processingTimer.setInterval(ms);
    }
    qDebug() << "Update rate set to" << ms << "ms";
}

void AudioManager::setWaveformDuration(double seconds)
{
    if (seconds < 0.01 || seconds > 10.0) {
        qWarning() << "Invalid waveform duration:" << seconds << "s (valid range: 0.01-10.0)";
        return;
    }

    m_waveformDurationS = seconds;
    qDebug() << "Waveform duration set to" << seconds << "s";
}

void AudioManager::setWaveformSamples(int samples)
{
    if (samples < 64 || samples > 8192) {
        qWarning() << "Invalid waveform samples:" << samples << "(valid range: 64-8192)";
        return;
    }

    m_waveformSamples = samples;
    qDebug() << "Waveform samples set to" << samples;
}

void AudioManager::handleRawData(SourceType /*type*/, const QString& /*id*/, const QByteArray& rawData)
{
    if (!m_isProcessing || rawData.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_dataMutex);

    // Controlar tamaño del buffer para evitar overflow
    if (m_pendingData.size() + rawData.size() > m_maxBufferSize) {
        qWarning() << "Audio buffer overflow - dropping old data";
        m_pendingData.clear();
    }

    m_pendingData.append(rawData);
}

void AudioManager::processAccumulatedData()
{
    if (!m_isProcessing || !m_formatValid) {
        return;
    }

    QMutexLocker locker(&m_dataMutex);
    if (m_pendingData.isEmpty()) {
        return;
    }

    // Tomar una copia de los datos y limpiar el buffer
    QByteArray dataToProcess = m_pendingData;
    m_pendingData.clear();
    locker.unlock();

    // Procesar los datos
    QElapsedTimer timer;
    timer.start();

    VisualizationData vizData = processAudioChunk(dataToProcess);

    qint64 processingTimeUs = timer.nsecsElapsed() / 1000;

    // Actualizar estadísticas
    updateStatistics(processingTimeUs, vizData.waveform.size());

    // Emitir datos para visualización
    emit visualizationDataReady(vizData);

    // Emitir estadísticas periódicamente
    if (m_chunksCount % (1000 / m_updateRateMs) == 0) {
        emit statisticsUpdated(getProcessingLoad(), m_totalSamples, m_avgLatencyMs);
    }
}

VisualizationData AudioManager::processAudioChunk(const QByteArray& data)
{
    VisualizationData vizData;
    vizData.timestamp = QDateTime::currentMSecsSinceEpoch();
    vizData.sampleRate = m_currentFormat.sampleRate();
    vizData.channels = m_currentFormat.channelCount();

    // Convertir datos raw a float
    QVector<float> samples = convertToFloat(data, m_currentFormat);

    if (samples.isEmpty()) {
        return vizData;
    }

    // Reducir a número deseado de muestras para waveform
    if (samples.size() > m_waveformSamples) {
        QVector<float> decimated;
        decimated.reserve(m_waveformSamples);

        int step = samples.size() / m_waveformSamples;
        for (int i = 0; i < m_waveformSamples; ++i) {
            int index = i * step;
            if (index < samples.size()) {
                decimated.append(samples[index]);
            }
        }
        vizData.waveform = decimated;
    } else {
        vizData.waveform = samples;
    }

    // Calcular espectro (FFT básico)
    vizData.spectrum = calculateSpectrum(samples);

    // Calcular niveles
    calculateLevels(samples, vizData.peakLevel, vizData.rmsLevel);

    return vizData;
}

QVector<float> AudioManager::convertToFloat(const QByteArray& data, const QAudioFormat& format)
{
    QVector<float> samples;

    if (data.isEmpty()) {
        return samples;
    }

    const char* rawData = data.constData();
    int bytesPerSample = format.bytesPerSample();
    int sampleCount = data.size() / bytesPerSample;

    samples.reserve(sampleCount);

    // Convertir según el formato
    switch (format.sampleFormat()) {
    case QAudioFormat::Int16: {
        const qint16* int16Data = reinterpret_cast<const qint16*>(rawData);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(int16Data[i] / 32768.0f);
        }
        break;
    }
    case QAudioFormat::Int32: {
        const qint32* int32Data = reinterpret_cast<const qint32*>(rawData);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(int32Data[i] / 2147483648.0f);
        }
        break;
    }
    case QAudioFormat::Float: {
        const float* floatData = reinterpret_cast<const float*>(rawData);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(floatData[i]);
        }
        break;
    }
    default:
        qWarning() << "Unsupported audio format for conversion";
        break;
    }

    return samples;
}

QVector<float> AudioManager::calculateSpectrum(const QVector<float>& samples)
{
    // FFT muy básico - en producción usarías una librería como FFTW
    QVector<float> spectrum;
    int spectrumSize = qMin(512, samples.size() / 2);
    spectrum.reserve(spectrumSize);

    // Simulación simple de espectro - reemplazar con FFT real
    for (int i = 0; i < spectrumSize; ++i) {
        float magnitude = 0.0f;
        if (i < samples.size()) {
            magnitude = qAbs(samples[i]);
        }
        spectrum.append(magnitude);
    }

    return spectrum;
}

void AudioManager::calculateLevels(const QVector<float>& samples, double& peak, double& rms)
{
    if (samples.isEmpty()) {
        peak = rms = 0.0;
        return;
    }

    double sum = 0.0;
    peak = 0.0;

    for (float sample : samples) {
        double absSample = qAbs(sample);
        peak = qMax(peak, absSample);
        sum += sample * sample;
    }

    rms = qSqrt(sum / samples.size());
}

void AudioManager::onFormatDetected(SourceType /*type*/, const QString& /*id*/, const QAudioFormat& format)
{
    qDebug() << "Audio format detected:" << format.sampleRate() << "Hz,"
             << format.channelCount() << "ch," << format.sampleFormat();

    m_currentFormat = format;
    m_formatValid = format.isValid();
    emit formatChanged(format);
}

void AudioManager::onSourceStateChanged(SourceType /*type*/, const QString& /*id*/, bool active)
{
    qDebug() << "Source state changed:" << (active ? "ACTIVE" : "INACTIVE");
}

double AudioManager::getProcessingLoad() const
{
    if (m_chunksCount == 0) return 0.0;

    double totalMs = m_totalProcTimeUs / 1000.0;
    double intervalMs = m_chunksCount * m_updateRateMs;
    return (totalMs / intervalMs) * 100.0;
}

qint64 AudioManager::getTotalSamplesProcessed() const
{
    return m_totalSamples;
}

double AudioManager::getAverageLatency() const
{
    return m_avgLatencyMs;
}

QAudioFormat AudioManager::getCurrentFormat() const
{
    return m_currentFormat;
}

void AudioManager::resetStatistics()
{
    m_totalProcTimeUs = 0;
    m_totalSamples = 0;
    m_chunksCount = 0;
    m_avgLatencyMs = 0.0;
}

void AudioManager::updateStatistics(qint64 processingTimeUs, int samplesProcessed)
{
    m_totalProcTimeUs += processingTimeUs;
    m_totalSamples += samplesProcessed;
    ++m_chunksCount;

    // Calcular latencia promedio basada en el tamaño del buffer
    if (m_formatValid && samplesProcessed > 0) {
        double bufferMs = (double)samplesProcessed / (m_currentFormat.sampleRate() * m_currentFormat.channelCount()) * 1000.0;
        m_avgLatencyMs = (m_avgLatencyMs * (m_chunksCount - 1) + bufferMs) / m_chunksCount;
    }
}
