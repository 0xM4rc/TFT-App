#include "include/audio_manager.h"
#include "include/data_structures/audio_statistics.h"
#include "qdatetime.h"
#include <QElapsedTimer>
#include <QDebug>
#include <QtMath>
#include <algorithm>

AudioManager::AudioManager( QObject* parent)
    : QObject(parent)
    , m_audioConfig()
    , m_processor(nullptr)
    , m_controller(new SourceController(this))
    , m_isProcessing(false)
    , m_formatValid(false)
    , m_updateRateMs(20)
    , m_waveformDurationS(2.0)
    , m_waveformSamples(1024)
    , m_maxBufferSize(1024 * 1024)
    , m_totalProcTimeUs(0)
    , m_totalSamples(0)
    , m_chunksCount(0)
    , m_avgLatencyMs(0.0)
{
    qDebug() << "[AM] AudioManager initialized with" << m_updateRateMs << "ms update rate";

    // Ajusta m_audioConfig según tus valores deseados
    m_audioConfig.fftSize            = m_waveformSamples * 2;  // 2048
    m_audioConfig.overlap            = m_waveformSamples;      // 1024
    m_audioConfig.spectrogramHistory = 150;
    m_audioConfig.waveformBufferSize = 8192;
    m_audioConfig.windowType         = AudioConfiguration::Hanning;

    // Crea y configura el AudioProcessor **una sola vez**
    m_processor = std::make_unique<AudioProcessor>(m_audioConfig, this);
    connect(m_processor.get(), &AudioProcessor::processingError,
            this, &AudioManager::onProcessorError);

    // Timer para procesar datos
    m_processingTimer.setInterval(m_updateRateMs);
    bool ok = connect(&m_processingTimer, &QTimer::timeout,
                      this, &AudioManager::processAccumulatedData);
    qDebug() << "[AM] Timer timeout connected =" << ok;

    //===============================================
    // SOURCE CONTROLLER SETUP
    //===============================================
    // connect(m_controller, &SourceController::dataReady,
    //         this, &AudioManager::handleRawData);
    // connect(m_controller, &SourceController::formatDetected,
    //         this, &AudioManager::onFormatDetected);
    // connect(m_controller, &SourceController::stateChanged,
    //         this, &AudioManager::onSourceStateChanged);

    network_source_id = m_controller->addNetworkSource(
        QUrl::fromUserInput(""));
    mic_source_id = QStringLiteral("mic1");
    m_controller->addMicrophoneSource(mic_source_id);

    connect(m_controller, &SourceController::dataReady,
            this, &AudioManager::handleRawData);


    connect(m_controller, &SourceController::stateChanged,
            this, [](SourceType type, const QString& id, bool active){
                qDebug() << "[AudioManager] Fuente" << id << "activa?" << active;
            });

    connect(m_controller, &SourceController::error,
            this, [](SourceType type, const QString& id, const QString& msg){
                qWarning() << "[AudioManager] Error en" << id << ":" << msg;
            });

    connect(m_controller, &SourceController::formatDetected,
            this, [](SourceType type, const QString& id, const QAudioFormat& fmt){
                qDebug() << "[AudioManager] Formato detectado en" << id << ":"
                         << fmt.sampleRate() << "Hz," << fmt.channelCount() << "canales";
            });
}


AudioManager::~AudioManager()
{
    stopProcessing();
}

bool AudioManager::fetchURL(const QUrl& input_url)
{
    // 1. Intentar actualizar la URL en el controller
    bool ok = m_controller->updateNetworkSource(network_source_id, input_url);
    if (!ok) {
        qWarning() << "[AudioManager] No se pudo actualizar la URL de la fuente de red (clave="
                   << network_source_id << ") a" << input_url.toString();
        return false;
    }
    qDebug() << "[AudioManager] URL de red actualizada a" << input_url.toString();

    // 2. Asegurarnos de que la fuente activa sea la de red
    if (m_controller->activeSourceKey() != network_source_id ||
        m_controller->activeSourceType() != SourceType::Network)
    {
        qDebug() << "[AudioManager] Cambiando fuente activa a la de red:" << network_source_id;
        if (!m_controller->setActiveSource(network_source_id)) {
            qWarning() << "[AudioManager] No se pudo activar la fuente de red:" << network_source_id;
            return false;
        }
    }

    // 3. Si la fuente de red no está corriendo, (re)iniciarla
    if (!m_controller->isActiveSourceRunning()) {
        qDebug() << "[AudioManager] Iniciando la fuente de red:" << network_source_id;
        m_controller->setActiveSource(network_source_id);
        m_controller->start();
    }
    return true;
}


bool AudioManager::fetchMicrophone(const QString& micKey, const QAudioDevice& newDevice)
{
    bool ok = m_controller->updateMicrophoneSource(micKey, newDevice);
    if (!ok) {
        qWarning() << "[AudioManager] No se pudo actualizar el dispositivo del micrófono (clave="
                   << micKey << ") a" << newDevice.description();
        return false;
    }
    qDebug() << "[AudioManager] Micrófono" << micKey << "actualizado a" << newDevice.description();
    return true;
}


void AudioManager::startProcessing()
{
    if (m_isProcessing) return;
    resetStatistics();

    m_currentFormat = m_controller->activeFormat();
    m_formatValid   = m_currentFormat.isValid();

    m_controller->start();
    m_processingTimer.start();

    // <<< Añade esto:
    if (m_processor) {
        m_processor->start();
    }

    m_isProcessing = true;
    emit processingStarted();
    qDebug() << "AudioManager processing started";
}


void AudioManager::stopProcessing()
{
    if (!m_isProcessing) return;

    m_processingTimer.stop();
    m_controller->stop();

    // <<< Añade esto:
    if (m_processor) {
        m_processor->stop();
    }

    // limpia buffers…
    m_isProcessing = false;
    emit processingStopped();
}




bool AudioManager::isProcessing() const
{
    return m_isProcessing;
}

int AudioManager::getUpdateRate() const
{
    return m_updateRateMs;
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
    qDebug() << "[DBG] handleRawData: rawData size =" << rawData.size();

    if (!m_isProcessing || rawData.isEmpty()) {
        qDebug() << "No raw data";
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
    qDebug() << "[DBG] processAccumulatedData() called."
             << "isProcessing=" << m_isProcessing
             << "formatValid="  << m_formatValid
             << "pendingDataSize=" << m_pendingData.size();

    if (!m_isProcessing || !m_formatValid || !m_processor) {
        qDebug() << "[DBG] early exit, state:"
                 << "isProcessing=" << m_isProcessing
                 << "formatValid="  << m_formatValid
                 << "processor="    << (m_processor != nullptr);
        return;
    }

    QByteArray raw;
    {
        QMutexLocker locker(&m_dataMutex);
        if (m_pendingData.isEmpty()) {
            qDebug() << "[DBG] no pending data, returning";
            return;
        }
        raw = m_pendingData;
        m_pendingData.clear();
    }
    qDebug() << "[DBG] Stole raw bytes, length =" << raw.size();

    QVector<float> samples = convertToFloat(raw, m_currentFormat);
    qDebug() << "[DBG] Converted to float samples, count =" << samples.size();
    if (samples.isEmpty()) {
        qWarning() << "[DBG] No samples after conversion";
        return;
    }

    // 1) Push samples al AudioProcessor
    if (!m_processor->pushAudioData(samples.constData(), samples.size(), m_currentFormat)) {
        qWarning() << "[DBG] pushAudioData failed";
        return;
    }

    // 2) Medir tiempo de procesamiento forzado
    QElapsedTimer timer; timer.start();
    qint64 procTimeUs = timer.nsecsElapsed() / 1000;
    qDebug() << "[DBG] forced processAudioBuffer in" << procTimeUs << "µs";

    // 3) Obtener VisualizationData
    VisualizationData vizData;
    bool ok = m_processor->getVisualizationData(vizData);
    qDebug() << "[DBG] getVisualizationData returned" << ok;
    if (!ok) {
        qWarning() << "[DBG] No visualization data ready";
        return;
    }

    qDebug() << "[DBG] vizData.waveform.size =" << vizData.waveform.size()
             << "spectrum.size =" << vizData.spectrum.size()
             << "peakLevel =" << vizData.peakLevel
             << "rmsLevel =" << vizData.rmsLevel;

    updateStatistics(procTimeUs, vizData.waveform.size());

    // Compute frequency resolution if spectrum provided
    if (!vizData.spectrum.isEmpty()) {
        vizData.frequencyResolution =
            (float)m_currentFormat.sampleRate() / vizData.spectrum.size();
    }

    // Emit visualization and periodic stats
    emit visualizationDataReady(vizData);
    if (m_chunksCount % (1000 / m_updateRateMs) == 0) {
        AudioStatistics stats = getStatistics();
        emit statisticsUpdated(
            stats.processingLoad,
            stats.totalSamples,
            stats.averageLatency
            );
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

    // Simulación simple de espectro basada en análisis por bandas
    int bandsPerBin = qMax(1, samples.size() / spectrumSize);

    for (int i = 0; i < spectrumSize; ++i) {
        float magnitude = 0.0f;
        int count = 0;

        // Promediar muestras en cada banda
        int startIdx = i * bandsPerBin;
        int endIdx = qMin(startIdx + bandsPerBin, samples.size());

        for (int j = startIdx; j < endIdx; ++j) {
            magnitude += qAbs(samples[j]);
            count++;
        }

        if (count > 0) {
            magnitude /= count;
            // Simular respuesta logarítmica típica del espectro
            magnitude = qSqrt(magnitude) * (1.0f + i * 0.001f);
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
    emit sourceStateChanged(active);
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

AudioStatistics AudioManager::getStatistics() const
{
    AudioStatistics stats;
    stats.processingLoad = getProcessingLoad();
    stats.totalSamples = m_totalSamples;
    stats.averageLatency = m_avgLatencyMs;
    stats.currentFormat = m_currentFormat;
    stats.formatValid = m_formatValid;
    stats.processingTime = m_totalProcTimeUs;
    stats.chunksProcessed = m_chunksCount;
    stats.updateDerivedStats();
    return stats;
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

void AudioManager::applyConfiguration(const AudioConfiguration& config)
{
    m_audioConfig = config;
    if (m_processor) {
        m_processor->setConfiguration(config);
    }
}

void AudioManager::onProcessorError(const QString& error)
{
    // Reenvía como señal pública
    emit processingError(error);
    qWarning() << "AudioManager - Processor error:" << error;
}
