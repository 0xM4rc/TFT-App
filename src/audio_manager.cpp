#include "include/audio_manager.h"
#include "include/audio_processor.h"
#include "include/interfaces/audio_source.h"
#include <QDebug>
#include <QDateTime>
#include <QtMath>

const double AudioManager::DEFAULT_WAVEFORM_DURATION = 2.0;

AudioManager::AudioManager(QObject *parent)
    : QObject(parent)
    , m_audioSource(nullptr)
    , m_processor(nullptr)
    , m_processingTimer(new QTimer(this))
    , m_updateRate(DEFAULT_UPDATE_RATE)
    , m_fftSize(DEFAULT_FFT_SIZE)
    , m_waveformDuration(DEFAULT_WAVEFORM_DURATION)
    , m_processingEnabled(true)
    , m_isProcessing(false)
    , m_totalProcessingTime(0)
    , m_totalSamplesProcessed(0)
    , m_processedChunks(0)
    , m_averageLatency(0.0)
{
    // Configurar timer de procesamiento
    m_processingTimer->setSingleShot(false);
    connect(m_processingTimer, &QTimer::timeout, this, &AudioManager::processAudioData);

    qDebug() << "AudioManager initialized";
}

AudioManager::~AudioManager()
{
    stopProcessing();
    disconnectSource();
    qDebug() << "AudioManager destroyed";
}

void AudioManager::setAudioSource(AudioSource* source)
{
    if (m_audioSource == source) return;

    // Detener procesamiento actual
    bool wasProcessing = m_isProcessing;
    if (wasProcessing) {
        stopProcessing();
    }

    // Desconectar fuente anterior
    disconnectSource();

    // Conectar nueva fuente
    m_audioSource = source;
    if (m_audioSource) {
        connectSource();
        m_currentSourceName = m_audioSource->sourceName();
        m_currentFormat = m_audioSource->format();

        emit sourceChanged(m_currentSourceName);
        emit formatChanged(m_currentFormat);

        qDebug() << "Audio source set to:" << m_currentSourceName;
        qDebug() << "Format:" << m_currentFormat.sampleRate() << "Hz,"
                 << m_currentFormat.channelCount() << "ch,"
                 << m_currentFormat.sampleFormat();
    }

    // Reiniciar procesamiento si estaba activo
    if (wasProcessing && m_audioSource) {
        startProcessing();
    }
}

void AudioManager::connectSource()
{
    if (!m_audioSource) return;

    connect(m_audioSource, &AudioSource::dataReady,
            this, &AudioManager::handleSourceDataReady);
    connect(m_audioSource, &AudioSource::stateChanged,
            this, &AudioManager::handleSourceStateChanged);
    connect(m_audioSource, &AudioSource::error,
            this, &AudioManager::handleSourceError);

    // Para NetworkSource que tiene detección automática de formato
    if (m_audioSource->metaObject()->className() == QString("NetworkSource")) {
        // Conectar señal de formato detectado si existe
        if (m_audioSource->metaObject()->indexOfSignal("formatDetected(QAudioFormat)") != -1) {
            connect(m_audioSource, SIGNAL(formatDetected(QAudioFormat)),
                    this, SLOT(handleSourceFormatDetected(QAudioFormat)));
        }
    }
}

void AudioManager::disconnectSource()
{
    if (!m_audioSource) return;

    disconnect(m_audioSource, nullptr, this, nullptr);
}

void AudioManager::startProcessing()
{
    if (m_isProcessing || !m_audioSource) {
        qWarning() << "Cannot start processing: already processing or no source";
        return;
    }

    // Crear processor si no existe
    if (!m_processor) {
        m_processor = std::make_unique<AudioProcessor>();
        m_processor->setFFTSize(m_fftSize);
    }

    // Iniciar fuente de audio
    if (!m_audioSource->isActive()) {
        m_audioSource->start();
    }

    // Resetear estadísticas
    resetStatistics();

    // Iniciar timer de procesamiento
    m_processingTimer->start(m_updateRate);
    m_isProcessing = true;

    emit processingStarted();
    qDebug() << "Audio processing started";
}

void AudioManager::stopProcessing()
{
    if (!m_isProcessing) return;

    m_processingTimer->stop();

    if (m_audioSource && m_audioSource->isActive()) {
        m_audioSource->stop();
    }

    m_isProcessing = false;

    // Limpiar datos pendientes
    QMutexLocker locker(&m_dataMutex);
    m_pendingData.clear();

    emit processingStopped();
    qDebug() << "Audio processing stopped";
}

bool AudioManager::isProcessing() const
{
    return m_isProcessing;
}

void AudioManager::setUpdateRate(int milliseconds)
{
    if (milliseconds < 10 || milliseconds > 1000) {
        qWarning() << "Invalid update rate:" << milliseconds << "ms. Using default.";
        return;
    }

    m_updateRate = milliseconds;

    if (m_isProcessing) {
        m_processingTimer->setInterval(m_updateRate);
    }

    qDebug() << "Update rate set to" << m_updateRate << "ms";
}

void AudioManager::setFFTSize(int size)
{
    // Verificar que sea potencia de 2
    if (size < 64 || size > 8192 || (size & (size - 1)) != 0) {
        qWarning() << "Invalid FFT size:" << size << ". Must be power of 2 between 64 and 8192.";
        return;
    }

    m_fftSize = size;

    if (m_processor) {
        m_processor->setFFTSize(m_fftSize);
    }

    qDebug() << "FFT size set to" << m_fftSize;
}

void AudioManager::setWaveformDuration(double seconds)
{
    if (seconds < 0.1 || seconds > 10.0) {
        qWarning() << "Invalid waveform duration:" << seconds << "s. Using default.";
        return;
    }

    m_waveformDuration = seconds;
    qDebug() << "Waveform duration set to" << m_waveformDuration << "seconds";
}

void AudioManager::setProcessingEnabled(bool enabled)
{
    m_processingEnabled = enabled;
    qDebug() << "Processing" << (enabled ? "enabled" : "disabled");
}

QAudioFormat AudioManager::currentFormat() const
{
    return m_currentFormat;
}

QString AudioManager::currentSourceName() const
{
    return m_currentSourceName;
}

double AudioManager::getProcessingLoad() const
{
    if (m_processedChunks == 0) return 0.0;

    double totalTimeMs = m_totalProcessingTime / 1000.0; // convert to ms
    double intervalMs = m_processedChunks * m_updateRate;

    return (totalTimeMs / intervalMs) * 100.0; // percentage
}

qint64 AudioManager::getTotalSamplesProcessed() const
{
    return m_totalSamplesProcessed;
}

double AudioManager::getAverageLatency() const
{
    return m_averageLatency;
}

void AudioManager::handleSourceDataReady()
{
    if (!m_audioSource || !m_isProcessing) return;

    QByteArray newData = m_audioSource->getData();
    if (newData.isEmpty()) return;

    // Agregar datos al buffer thread-safe
    QMutexLocker locker(&m_dataMutex);
    m_pendingData.append(newData);
}

void AudioManager::handleSourceStateChanged(bool active)
{
    qDebug() << "Source state changed:" << (active ? "active" : "inactive");

    if (!active && m_isProcessing) {
        // La fuente se detuvo inesperadamente
        emit error("Audio source stopped unexpectedly");
    }
}

void AudioManager::handleSourceError(const QString& error)
{
    qWarning() << "Source error:" << error;
    emit this->error(QString("Audio source error: %1").arg(error));
}

void AudioManager::handleSourceFormatDetected(const QAudioFormat& format)
{
    qDebug() << "Format detected:" << format.sampleRate() << "Hz,"
             << format.channelCount() << "ch";

    m_currentFormat = format;
    emit formatChanged(m_currentFormat);

    // Reconfigurar processor con nuevo formato
    if (m_processor) {
        // El processor se adaptará al nuevo formato automáticamente
    }
}

void AudioManager::processAudioData()
{
    if (!m_processingEnabled || !m_processor) return;

    QElapsedTimer timer;
    timer.start();

    // Obtener datos pendientes
    QByteArray dataToProcess;
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_pendingData.isEmpty()) return;

        dataToProcess = m_pendingData;
        m_pendingData.clear();
    }

    // Convertir bytes a samples float
    QVector<float> samples = convertBytesToFloat(dataToProcess, m_currentFormat);
    if (samples.isEmpty()) return;

    // Procesar con AudioProcessor
    VisualizationData vizData;
    if (m_processor->processAudioData(samples, m_currentFormat, vizData)) {
        // Calcular niveles de audio
        calculateLevels(samples, vizData.peakLevel, vizData.rmsLevel);

        // Timestamp
        vizData.timestamp = QDateTime::currentMSecsSinceEpoch();

        // Emitir datos procesados
        emit visualizationDataReady(vizData);
    }

    // Actualizar estadísticas
    qint64 processingTime = timer.nsecsElapsed() / 1000; // microseconds
    m_totalProcessingTime += processingTime;
    m_totalSamplesProcessed += samples.size();
    m_processedChunks++;

    // Calcular latencia promedio (estimada)
    double currentLatency = (double)dataToProcess.size() /
                            (m_currentFormat.sampleRate() * m_currentFormat.channelCount() *
                             (m_currentFormat.sampleFormat() == QAudioFormat::Int16 ? 2 : 4)) * 1000.0;
    m_averageLatency = (m_averageLatency * (m_processedChunks - 1) + currentLatency) / m_processedChunks;

    // Emitir estadísticas cada 30 chunks (~1 segundo)
    if (m_processedChunks % 30 == 0) {
        emit statisticsUpdated(getProcessingLoad(), m_totalSamplesProcessed, m_averageLatency);
    }
}

QVector<float> AudioManager::convertBytesToFloat(const QByteArray& data, const QAudioFormat& format)
{
    QVector<float> samples;

    if (data.isEmpty()) return samples;

    const int bytesPerSample = format.bytesPerSample();
    const int sampleCount = data.size() / bytesPerSample;
    samples.reserve(sampleCount);

    const char* dataPtr = data.constData();

    switch (format.sampleFormat()) {
    case QAudioFormat::Int16: {
        const qint16* int16Data = reinterpret_cast<const qint16*>(dataPtr);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(int16Data[i] / 32768.0f); // Normalizar a [-1.0, 1.0]
        }
        break;
    }
    case QAudioFormat::Int32: {
        const qint32* int32Data = reinterpret_cast<const qint32*>(dataPtr);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(int32Data[i] / 2147483648.0f); // Normalizar a [-1.0, 1.0]
        }
        break;
    }
    case QAudioFormat::Float: {
        const float* floatData = reinterpret_cast<const float*>(dataPtr);
        for (int i = 0; i < sampleCount; ++i) {
            samples.append(floatData[i]); // Ya está normalizado
        }
        break;
    }
    default:
        qWarning() << "Unsupported sample format:" << format.sampleFormat();
        break;
    }

    return samples;
}

void AudioManager::calculateLevels(const QVector<float>& samples, double& peak, double& rms)
{
    if (samples.isEmpty()) {
        peak = rms = 0.0;
        return;
    }

    double sumSquares = 0.0;
    peak = 0.0;

    for (float sample : samples) {
        double absSample = qAbs(sample);
        if (absSample > peak) {
            peak = absSample;
        }
        sumSquares += sample * sample;
    }

    rms = qSqrt(sumSquares / samples.size());
}

void AudioManager::resetStatistics()
{
    m_totalProcessingTime = 0;
    m_totalSamplesProcessed = 0;
    m_processedChunks = 0;
    m_averageLatency = 0.0;
}
