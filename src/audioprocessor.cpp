#include "include/audioprocessor.h"
#include <QDebug>
#include <QCoreApplication>

AudioProcessor::AudioProcessor(QObject *parent)
    : QObject(parent)
    , m_buffer(QSharedPointer<AudioBuffer>::create())
    , m_processingTimer(new QTimer())
    , m_processingThread(new QThread(this))
    , m_running(false)
    , m_processingInterval(50) // 50ms default
{
    // Mover el timer al thread de procesamiento
    m_processingTimer->moveToThread(m_processingThread);

    // Conectar señales del timer
    connect(m_processingTimer, &QTimer::timeout, this, &AudioProcessor::processAudioData);

    // Conectar señales del thread
    connect(m_processingThread, &QThread::started, [this]() {
        m_processingTimer->start(m_processingInterval);
        qDebug() << "Processing thread started";
    });

    connect(m_processingThread, &QThread::finished, [this]() {
        m_processingTimer->stop();
        qDebug() << "Processing thread finished";
    });
}

AudioProcessor::~AudioProcessor()
{
    stop();
    m_processingThread->quit();
    m_processingThread->wait();
}

void AudioProcessor::addSource(QSharedPointer<AudioSource> source)
{
    QMutexLocker locker(&m_sourcesMutex);

    if (m_sources.contains(source)) {
        qWarning() << "Source already added";
        return;
    }

    m_sources.append(source);

    // Conectar señales de la fuente
    connect(source.data(), &AudioSource::dataReady,
            this, &AudioProcessor::handleSourceDataReady, Qt::QueuedConnection);
    connect(source.data(), &AudioSource::error,
            this, &AudioProcessor::handleSourceError, Qt::QueuedConnection);
    connect(source.data(), &AudioSource::stateChanged,
            this, &AudioProcessor::handleSourceStateChanged, Qt::QueuedConnection);

    qDebug() << "Added audio source:" << source->sourceName();
}

void AudioProcessor::removeSource(QSharedPointer<AudioSource> source)
{
    QMutexLocker locker(&m_sourcesMutex);

    int index = m_sources.indexOf(source);
    if (index != -1) {
        // Desconectar señales
        disconnect(source.data(), nullptr, this, nullptr);

        // Detener la fuente si está activa
        if (source->isActive()) {
            source->stop();
        }

        m_sources.removeAt(index);
        qDebug() << "Removed audio source:" << source->sourceName();
    }
}

void AudioProcessor::removeSource(const QString &sourceName)
{
    QMutexLocker locker(&m_sourcesMutex);

    for (int i = m_sources.size() - 1; i >= 0; --i) {
        if (m_sources[i]->sourceName() == sourceName) {
            auto source = m_sources[i];
            disconnect(source.data(), nullptr, this, nullptr);

            if (source->isActive()) {
                source->stop();
            }

            m_sources.removeAt(i);
            qDebug() << "Removed audio source:" << sourceName;
            break;
        }
    }
}

void AudioProcessor::start()
{
    if (m_running) return;

    m_running = true;

    // Iniciar todas las fuentes
    QMutexLocker locker(&m_sourcesMutex);
    for (auto &source : m_sources) {
        source->start();
    }
    locker.unlock();

    // Iniciar el thread de procesamiento
    m_processingThread->start();

    emit processingStarted();
    qDebug() << "Audio processor started";
}

void AudioProcessor::stop()
{
    if (!m_running) return;

    m_running = false;

    // Detener todas las fuentes
    QMutexLocker locker(&m_sourcesMutex);
    for (auto &source : m_sources) {
        source->stop();
    }
    locker.unlock();

    // Detener el thread de procesamiento
    m_processingThread->quit();
    m_processingThread->wait(3000); // Wait up to 3 seconds

    emit processingStopped();
    qDebug() << "Audio processor stopped";
}

bool AudioProcessor::isRunning() const
{
    return m_running;
}

void AudioProcessor::setProcessingInterval(int milliseconds)
{
    m_processingInterval = milliseconds;
    if (m_running) {
        QMetaObject::invokeMethod(m_processingTimer, "start", Qt::QueuedConnection,
                                  Q_ARG(int, m_processingInterval));
    }
}

void AudioProcessor::setBufferSize(int maxChunks)
{
    m_buffer->setMaxSize(maxChunks);
}

QSharedPointer<AudioBuffer> AudioProcessor::getBuffer() const
{
    return m_buffer;
}

QList<QSharedPointer<AudioSource>> AudioProcessor::getSources() const
{
    QMutexLocker locker(&m_sourcesMutex);
    return m_sources;
}

void AudioProcessor::processAudioData()
{
    QMutexLocker locker(&m_sourcesMutex);

    for (auto &source : m_sources) {
        if (source->isActive()) {
            QByteArray data = source->getData();
            if (!data.isEmpty()) {
                // Convertir a formato común si es necesario
                QByteArray convertedData = convertToCommonFormat(data, source->format());

                // Crear chunk y agregarlo al buffer
                AudioChunk chunk(convertedData, source->sourceName(), getCommonFormat());
                m_buffer->addChunk(chunk);

                emit dataProcessed(chunk);
            }
        }
    }
}

void AudioProcessor::handleSourceDataReady()
{
    // La fuente tiene datos listos, se procesarán en el próximo ciclo del timer
}

void AudioProcessor::handleSourceError(const QString &error)
{
    AudioSource *source = qobject_cast<AudioSource*>(sender());
    if (source) {
        qWarning() << "Error from source" << source->sourceName() << ":" << error;
        emit this->error(QString("Source %1: %2").arg(source->sourceName(), error));
    }
}

void AudioProcessor::handleSourceStateChanged(bool active)
{
    AudioSource *source = qobject_cast<AudioSource*>(sender());
    if (source) {
        qDebug() << "Source" << source->sourceName() << "state changed to" << (active ? "active" : "inactive");
    }
}

QByteArray AudioProcessor::convertToCommonFormat(const QByteArray &data, const QAudioFormat &sourceFormat)
{
    // Por simplicidad, asumimos que todos los formatos son compatibles
    // En una implementación real, aquí haríamos la conversión de formato
    Q_UNUSED(sourceFormat)
    return data;
}

QAudioFormat AudioProcessor::getCommonFormat() const
{
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    return format;
}
