#include "include/source_controller.h"
#include "include/network_source.h"
#include "include/microphone_source.h"
#include <QDebug>

SourceController::SourceController(QObject* parent)
    : QObject(parent)
{
    qDebug() << "SourceController initialized";
}

SourceController::~SourceController()
{
    qDebug() << "SourceController destructor started";

    // Detener todas las fuentes activas de manera segura
    stopAllSources();

    // Limpiar todas las fuentes
    clearAllSources();

    qDebug() << "SourceController destructor completed";
}

QString SourceController::addNetworkSource(const QUrl& url) {
    // Crear NetworkSource con SourceController como padre
    NetworkSource* source = new NetworkSource(url, this);

    // Agregar a la colección
    m_sources.insert(source->sourceId(), source);

    emit sourceAdded(source->sourceId());
    qDebug() << "NetworkSource added:" << source->sourceId() << "URL:" << url.toString();

    // Retornar el UID del source
    return source->sourceId();
}

bool SourceController::addMicrophoneSource(const QString& key, const QAudioDevice& device)
{
    if (m_sources.contains(key)) {
        qWarning() << "Source key already exists:" << key;
        return false;
    }

    // Crear MicrophoneSource con SourceController como padre
    MicrophoneSource* source = new MicrophoneSource(this);

    if (device.isNull()) {
        source->setDevice(QMediaDevices::defaultAudioInput());
    } else {
        source->setDevice(device);
    }

    // Agregar a la colección
    m_sources.insert(key, source);

    emit sourceAdded(key);
    qDebug() << "MicrophoneSource added:" << key << "Device:" << device.description();

    return true;
}

void SourceController::removeSource(const QString& key)
{
    if (!m_sources.contains(key)) {
        qWarning() << "Source not found:" << key;
        return;
    }

    AudioSource* source = m_sources[key];

    // Si es la fuente activa, detenerla primero
    if (m_activeKey == key) {
        source->stop();
        disconnectCurrent();
        m_activeKey.clear();
    }

    // Desconectar todas las señales
    source->disconnect(this);

    // Remover de la colección y eliminar
    m_sources.remove(key);
    source->deleteLater();

    emit sourceRemoved(key);
    qDebug() << "Source removed and scheduled for deletion:" << key;
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

    // Detener la fuente anterior
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
        AudioSource* source = m_sources[m_activeKey];
        return source->isActive();
    }
    return false;
}

QStringList SourceController::availableSources() const
{
    return m_sources.keys();
}

QString SourceController::activeSourceKey() const
{
    return m_activeKey;
}

SourceType SourceController::activeSourceType() const
{
    if (hasActiveSource()) {
        return m_sources[m_activeKey]->sourceType();
    }
    return SourceType::Network; // valor por defecto
}

void SourceController::stopAllSources()
{
    qDebug() << "Stopping all sources...";

    for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
        AudioSource* source = it.value();
        if (source && source->isActive()) {
            source->stop();
        }
    }

    // Desconectar fuente activa
    if (hasActiveSource()) {
        disconnectCurrent();
        m_activeKey.clear();
    }

    qDebug() << "All sources stopped";
}

void SourceController::clearAllSources()
{
    qDebug() << "Clearing all sources...";

    // Desconectar todas las señales y eliminar fuentes
    for (AudioSource* source : m_sources) {
        if (source) {
            source->disconnect(this);
            source->deleteLater();
        }
    }

    m_sources.clear();
    m_activeKey.clear();

    qDebug() << "All sources scheduled for deletion";
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
    // Obtener la fuente que envió la señal
    AudioSource* source = qobject_cast<AudioSource*>(sender());
    if (!source) return;

    // Verificar que sea la fuente activa
    if (m_sources.value(m_activeKey, nullptr) != source) {
        return; // Ignorar datos de fuentes inactivas
    }

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
    // Verificar que sea la fuente activa
    AudioSource* source = qobject_cast<AudioSource*>(sender());
    if (!source || m_sources.value(m_activeKey, nullptr) != source) {
        return;
    }

    emit stateChanged(type, id, active);
}

void SourceController::onSourceError(SourceType type, const QString& id, const QString& message)
{
    qWarning() << "Source error [" << static_cast<int>(type) << id << "]:" << message;
    emit error(type, id, message);
}

void SourceController::onSourceFormatDetected(SourceType type, const QString& id, const QAudioFormat& format)
{
    // Verificar que sea la fuente activa
    AudioSource* source = qobject_cast<AudioSource*>(sender());
    if (!source || m_sources.value(m_activeKey, nullptr) != source) {
        return;
    }

    qDebug() << "Format detected [" << static_cast<int>(type) << id << "]:"
             << format.sampleRate() << "Hz," << format.channelCount() << "ch";
    emit formatDetected(type, id, format);
}

bool SourceController::updateNetworkSource(const QString& key, const QUrl& newUrl)
{
    // Busca la fuente
    auto it = m_sources.find(key);
    if (it == m_sources.end()) {
        qWarning() << "Cannot update NetworkSource - key not found:" << key;
        return false;
    }

    // Comprueba que sea un NetworkSource
    if (auto netSrc = qobject_cast<NetworkSource*>(it.value())) {
        if (netSrc->isActive()) {
            qWarning() << "Cannot change URL while source is active. Stop it first:" << key;
            return false;
        }
        netSrc->setUrl(newUrl);
        qDebug() << "NetworkSource" << key << "URL updated to" << newUrl.toString();
        emit sourceUpdated(key);
        return true;
    } else {
        qWarning() << "Source with key is not a NetworkSource:" << key;
        return false;
    }
}

bool SourceController::updateMicrophoneSource(const QString& key, const QAudioDevice& newDevice)
{
    auto it = m_sources.find(key);
    if (it == m_sources.end()) {
        qWarning() << "Cannot update MicrophoneSource - key not found:" << key;
        return false;
    }

    if (auto micSrc = qobject_cast<MicrophoneSource*>(it.value())) {
        if (micSrc->isActive()) {
            qWarning() << "Cannot change device while source is active. Stop it first:" << key;
            return false;
        }
        micSrc->setDevice(newDevice);
        qDebug() << "MicrophoneSource" << key << "device updated to" << newDevice.description();
        emit sourceUpdated(key);
        return true;
    } else {
        qWarning() << "Source with key is not a MicrophoneSource:" << key;
        return false;
    }
}
