#include "include/source_controller.h"
#include "include/network_source.h"
#include <QDebug>

SourceController::SourceController(QObject* parent)
    : QObject(parent)
{
}

SourceController::~SourceController()
{
    if (hasActiveSource()) {
        stop();
        disconnectCurrent();
    }
}

void SourceController::addSource(const QString& key, AudioSource* source)
{
    if (!source) {
        qWarning() << "Cannot add null source with key:" << key;
        return;
    }

    m_sources.insert(key, source);
    emit sourceAdded(key);
    qDebug() << "Source added:" << key << "Type:" << static_cast<int>(source->sourceType());
}

void SourceController::removeSource(const QString& key)
{
    if (!m_sources.contains(key)) {
        qWarning() << "Source not found:" << key;
        return;
    }

    // Si es la fuente activa, detenerla primero
    if (m_activeKey == key) {
        stop();
        disconnectCurrent();
        m_activeKey.clear();
    }

    m_sources.take(key);
    emit sourceRemoved(key);
    qDebug() << "Source removed:" << key;
}

bool SourceController::setActiveSource(const QString& key)
{
    if (!m_sources.contains(key)) {
        qWarning() << "Cannot set active source - key not found:" << key;
        return false;
    }

    if (m_activeKey == key) {
        qDebug() << "Source already active:" << key;
        return true;
    }

    // Detener y desconectar la fuente anterior
    if (hasActiveSource()) {
        m_sources[m_activeKey]->stop();
        disconnectCurrent();
    }

    // Activar la nueva fuente
    m_activeKey = key;
    connectCurrent();

    AudioSource* source = m_sources[m_activeKey];
    qDebug() << "Active source changed to:" << key << "Type:" << static_cast<int>(source->sourceType());

    return true;
}

void SourceController::start()
{
    if (!hasActiveSource()) {
        qWarning() << "No active source to start";
        return;
    }

    m_sources[m_activeKey]->start();
    qDebug() << "Started active source:" << m_activeKey;
}

void SourceController::stop()
{
    if (!hasActiveSource()) {
        qWarning() << "No active source to stop";
        return;
    }

    m_sources[m_activeKey]->stop();
    qDebug() << "Stopped active source:" << m_activeKey;
}

QAudioFormat SourceController::activeFormat() const
{
    if (hasActiveSource()) {
        return m_sources[m_activeKey]->format();
    }
    return QAudioFormat(); // formato inválido
}

bool SourceController::isActiveSourceRunning() const
{
    if (hasActiveSource()) {
        return m_sources[m_activeKey]->isActive();
    }
    return false;
}

void SourceController::connectCurrent()
{
    AudioSource* source = m_sources.value(m_activeKey, nullptr);
    if (!source) return;

    // Conectar señales base
    connect(source, &AudioSource::dataReady,
            this, &SourceController::onSourceDataReady);
    connect(source, &AudioSource::stateChanged,
            this, &SourceController::onSourceStateChanged);
    connect(source, &AudioSource::error,
            this, &SourceController::onSourceError);

    // Conectar señales específicas si es NetworkSource
    if (auto netSource = qobject_cast<NetworkSource*>(source)) {
        connect(netSource, &NetworkSource::formatDetected,
                this, &SourceController::onSourceFormatDetected);
    }

    emit activeSourceChanged(source->sourceType(), source->sourceId());
}

void SourceController::disconnectCurrent()
{
    if (AudioSource* source = m_sources.value(m_activeKey, nullptr)) {
        source->disconnect(this);
    }
}

void SourceController::onSourceDataReady()
{
    AudioSource* source = m_sources.value(m_activeKey, nullptr);
    if (!source) return;

    // Obtener datos y metadatos
    SourceType type = source->sourceType();
    QString id = source->sourceId();
    QByteArray data = source->getData();

    if (!data.isEmpty()) {
        emit dataReady(type, id, data);
    }
}

void SourceController::onSourceStateChanged(SourceType type, const QString& id, bool active)
{
    emit stateChanged(type, id, active);
}

void SourceController::onSourceError(SourceType type, const QString& id, const QString& message)
{
    qWarning() << "Source error [" << static_cast<int>(type) << id << "]:" << message;
    emit error(type, id, message);
}

void SourceController::onSourceFormatDetected(SourceType type, const QString& id, const QAudioFormat& format)
{
    qDebug() << "Format detected [" << static_cast<int>(type) << id << "]:"
             << format.sampleRate() << "Hz," << format.channelCount() << "ch";
    emit formatDetected(type, id, format);
}
