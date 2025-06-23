#include "include/network_source.h"
#include <QDebug>
#include <QDateTime>
#include <QCoreApplication>

// HOW TO USE
// NetworkSource *source = new NetworkSource();
// source->setUrl(QUrl("http://stream.example.com/audio"));

// // Conectar señal para saber cuando se detecta el formato
// connect(source, &NetworkSource::formatDetected, [](const QAudioFormat &format) {
//     qDebug() << "Formato detectado automáticamente:";
//     qDebug() << "Sample Rate:" << format.sampleRate();
//     qDebug() << "Canales:" << format.channelCount();
//     qDebug() << "Formato:" << format.sampleFormat();
// });

// source->start();

// Contador estático para instancias
int NetworkSource::s_instanceCounter = 0;

NetworkSource::NetworkSource(const QUrl &url, QObject *parent)
    : AudioSource(parent)
    , m_pipeline(nullptr)
    , m_source(nullptr)
    , m_audioconvert(nullptr)
    , m_audioresample(nullptr)
    , m_appsink(nullptr)
    , m_bus(nullptr)
    , m_busWatchId(0)
    , m_streamUrl(url)
    , m_reconnectTimer(new QTimer(this))
    , m_healthCheckTimer(new QTimer(this))
    , m_reconnectAttempts(0)
    , m_gstreamerInitialized(false)
    , m_pipelineCreated(false)
    , m_lastDataTime(0)
    , m_formatDetected(false)
    , m_creationTime(QDateTime::currentDateTime())
{
    // Generar identificadores únicos automáticamente
    generateUniqueIdentifiers();

    // Configurar formato por defecto (fallback)
    m_format.setSampleRate(44100);
    m_format.setChannelCount(2);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Configurar timers
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkSource::reconnectTimer);

    m_healthCheckTimer->setInterval(HEALTH_CHECK_INTERVAL);
    connect(m_healthCheckTimer, &QTimer::timeout, this, &NetworkSource::checkStreamHealth);

    // Inicializar GStreamer
    initializeGStreamer();

    qDebug() << "NetworkSource created with ID:" << m_sourceId << "Name:" << m_sourceName;
}

NetworkSource::NetworkSource(QObject *parent)
    : NetworkSource(QUrl(), parent)
{
    // Constructor delegado - el trabajo se hace en el constructor principal
}

NetworkSource::~NetworkSource()
{
    stop();
    destroyPipeline();

    if (m_gstreamerInitialized) {
        gst_deinit();
    }
}

void NetworkSource::generateUniqueIdentifiers()
{
    // Generar UUID único
    m_uuid = QUuid::createUuid();

    // Incrementar contador de instancias
    s_instanceCounter++;

    // Crear ID único usando prefijo + número de instancia + timestamp + UUID corto
    QString shortUuid = m_uuid.toString(QUuid::WithoutBraces).left(8);
    qint64 timestamp = m_creationTime.toMSecsSinceEpoch();

    m_sourceId = QString("NetSrc_%1_%2_%3")
                     .arg(s_instanceCounter, 3, 10, QChar('0'))
                     .arg(timestamp % 100000)  // Últimos 5 dígitos del timestamp
                     .arg(shortUuid);

    // Generar nombre friendly
    m_sourceName = generateFriendlyName();
}

QString NetworkSource::generateFriendlyName()
{
    // Si hay URL, usar información de ella para el nombre
    if (!m_streamUrl.isEmpty()) {
        QString host = m_streamUrl.host();
        QString path = m_streamUrl.path();

        if (!host.isEmpty()) {
            // Usar host y parte del path si existe
            if (!path.isEmpty() && path != "/") {
                QString pathPart = path.split("/").last();
                if (!pathPart.isEmpty()) {
                    return QString("Stream_%1_%2").arg(host).arg(pathPart);
                }
            }
            return QString("Stream_%1").arg(host);
        }
    }

    // Nombre genérico basado en timestamp y contador
    QString timeStr = m_creationTime.toString("hhmm");
    return QString("NetworkSource_%1_%2").arg(s_instanceCounter).arg(timeStr);
}

void NetworkSource::initializeGStreamer()
{
    if (m_gstreamerInitialized) return;

    GError *error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        qCritical() << "Failed to initialize GStreamer:" << (error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return;
    }

    m_gstreamerInitialized = true;
    qDebug() << "GStreamer initialized successfully for" << m_sourceId;
}

void NetworkSource::createPipeline()
{
    if (!m_gstreamerInitialized || m_pipelineCreated) return;

    // Crear pipeline con nombre único
    QString pipelineName = QString("audio-network-pipeline-%1").arg(m_sourceId);
    m_pipeline = gst_pipeline_new(pipelineName.toUtf8().constData());
    if (!m_pipeline) {
        qCritical() << "Failed to create GStreamer pipeline for" << m_sourceId;
        return;
    }

    // Crear elementos con nombres únicos
    QString sourceBaseName = QString("source-%1").arg(m_sourceId);
    QString convertBaseName = QString("audioconvert-%1").arg(m_sourceId);
    QString resampleBaseName = QString("audioresample-%1").arg(m_sourceId);
    QString sinkBaseName = QString("appsink-%1").arg(m_sourceId);

    m_source = gst_element_factory_make("uridecodebin", sourceBaseName.toUtf8().constData());
    m_audioconvert = gst_element_factory_make("audioconvert", convertBaseName.toUtf8().constData());
    m_audioresample = gst_element_factory_make("audioresample", resampleBaseName.toUtf8().constData());
    m_appsink = gst_element_factory_make("appsink", sinkBaseName.toUtf8().constData());

    if (!m_source || !m_audioconvert || !m_audioresample || !m_appsink) {
        qCritical() << "Failed to create GStreamer elements for" << m_sourceId;
        destroyPipeline();
        return;
    }

    // Configurar appsink básico (sin caps específicas aún)
    g_object_set(m_appsink,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "max-buffers", 100,
                 "drop", TRUE,
                 nullptr);

    // Conectar callbacks
    GstAppSinkCallbacks callbacks = {
        NetworkSource::onEos,
        nullptr,
        NetworkSource::onNewSample,
        {nullptr}
    };
    gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), &callbacks, this, nullptr);

    // Conectar pad-added signal para uridecodebin
    g_signal_connect(m_source, "pad-added", G_CALLBACK(NetworkSource::onPadAdded), this);

    // Agregar elementos al pipeline
    gst_bin_add_many(GST_BIN(m_pipeline),
                     m_source, m_audioconvert, m_audioresample, m_appsink, nullptr);

    // Enlazar elementos estáticos
    if (!gst_element_link_many(m_audioconvert, m_audioresample, m_appsink, nullptr)) {
        qCritical() << "Failed to link GStreamer elements for" << m_sourceId;
        destroyPipeline();
        return;
    }

    // Configurar bus
    m_bus = gst_element_get_bus(m_pipeline);
    m_busWatchId = gst_bus_add_watch(m_bus, NetworkSource::onBusMessage, this);

    m_pipelineCreated = true;
    m_formatDetected = false;  // Reset detection flag
    qDebug() << "GStreamer pipeline created successfully for" << m_sourceId;
}

void NetworkSource::destroyPipeline()
{
    if (!m_pipelineCreated) return;

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    if (m_bus) {
        if (m_busWatchId > 0) {
            g_source_remove(m_busWatchId);
            m_busWatchId = 0;
        }
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    m_source = nullptr;
    m_audioconvert = nullptr;
    m_audioresample = nullptr;
    m_appsink = nullptr;
    m_pipelineCreated = false;

    qDebug() << "GStreamer pipeline destroyed for" << m_sourceId;
}

void NetworkSource::start()
{
    if (m_active || m_streamUrl.isEmpty() || !m_gstreamerInitialized) {
        if (m_streamUrl.isEmpty()) {
            qWarning() << "Cannot start" << m_sourceId << "- no URL set";
        }
        return;
    }

    createPipeline();
    if (!m_pipelineCreated) return;

    // Configurar URL
    g_object_set(m_source, "uri", m_streamUrl.toString().toUtf8().constData(), nullptr);

    // Iniciar pipeline
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCritical() << "Failed to start GStreamer pipeline for" << m_sourceId;
        destroyPipeline();
        return;
    }

    m_active = true;
    m_reconnectAttempts = 0;
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
    m_healthCheckTimer->start();

    emit stateChanged(sourceType(), sourceId(), m_active);
    qDebug() << "Network source" << m_sourceId << "started with URL:" << m_streamUrl.toString();
}

void NetworkSource::stop()
{
    if (!m_active) return;

    m_reconnectTimer->stop();
    m_healthCheckTimer->stop();

    destroyPipeline();

    m_active = false;
    m_reconnectAttempts = 0;

    // Limpiar buffer
    QMutexLocker locker(&m_bufferMutex);
    m_buffer.clear();

    emit stateChanged(sourceType(), sourceId(), m_active);
    qDebug() << "Network source" << m_sourceId << "stopped";
}

bool NetworkSource::isActive() const
{
    if (!m_active || !m_pipeline) return false;

    GstState state;
    gst_element_get_state(m_pipeline, &state, nullptr, 0);
    return state == GST_STATE_PLAYING;
}

QByteArray NetworkSource::getData()
{
    QMutexLocker locker(&m_bufferMutex);
    QByteArray data = std::move(m_buffer);
    m_buffer.clear();
    return data;
}

QAudioFormat NetworkSource::format() const
{
    QMutexLocker locker(&const_cast<NetworkSource*>(this)->m_formatMutex);
    return m_formatDetected ? m_detectedFormat : m_format;
}

void NetworkSource::setUrl(const QUrl &url)
{
    if (m_active) {
        qWarning() << "Cannot change URL for" << m_sourceId << "while active";
        return;
    }

    QUrl oldUrl = m_streamUrl;
    m_streamUrl = url;

    // Regenerar nombre si cambió la URL significativamente
    if (oldUrl.host() != url.host() || oldUrl.path() != url.path()) {
        m_sourceName = generateFriendlyName();
        qDebug() << "Updated name for" << m_sourceId << "to:" << m_sourceName;
    }
}

void NetworkSource::setStreamFormat(const QAudioFormat &format)
{
    if (m_active) {
        qWarning() << "Cannot change format for" << m_sourceId << "while active";
        return;
    }
    m_format = format;
}

void NetworkSource::reconnectTimer()
{
    if (m_active && m_gstreamerInitialized) {
        qDebug() << "Attempting to reconnect" << m_sourceId << "...";

        // Recrear pipeline
        destroyPipeline();
        createPipeline();

        if (m_pipelineCreated) {
            g_object_set(m_source, "uri", m_streamUrl.toString().toUtf8().constData(), nullptr);

            GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
            if (ret != GST_STATE_CHANGE_FAILURE) {
                m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
                m_healthCheckTimer->start();
                qDebug() << "Reconnection successful for" << m_sourceId;
                return;
            }
        }

        // Si falló la reconexión
        handleReconnection();
    }
}

void NetworkSource::checkStreamHealth()
{
    if (!m_active) return;

    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastDataTime > STREAM_TIMEOUT) {
        qWarning() << "Stream timeout detected for" << m_sourceId << ", attempting reconnection";
        handleReconnection();
    }
}

void NetworkSource::handleReconnection()
{
    if (m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        m_reconnectAttempts++;
        int delay = m_reconnectAttempts * 2000; // Exponential backoff

        qDebug() << "Scheduling reconnection for" << m_sourceId << "in" << delay
                 << "ms (attempt" << m_reconnectAttempts << ")";

        m_healthCheckTimer->stop();
        if (m_pipeline) {
            gst_element_set_state(m_pipeline, GST_STATE_NULL);
        }

        m_reconnectTimer->start(delay);
    } else {
        qWarning() << "Max reconnection attempts reached for" << m_sourceId;
        stop();
        emit error(sourceType(), sourceId(), "Max reconnection attempts reached");
    }
}

// Static callbacks
GstFlowReturn NetworkSource::onNewSample(GstAppSink *sink, gpointer user_data)
{
    NetworkSource *source = static_cast<NetworkSource*>(user_data);

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (buffer) {
        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            {
                QMutexLocker locker(&source->m_bufferMutex);
                source->m_buffer.append(reinterpret_cast<const char*>(map.data), map.size);
            }

            source->m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
            emit source->dataReady(source->sourceType(), source->sourceId());

            gst_buffer_unmap(buffer, &map);
        }
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void NetworkSource::onEos(GstAppSink *sink, gpointer user_data)
{
    Q_UNUSED(sink)
    NetworkSource *source = static_cast<NetworkSource*>(user_data);
    qDebug() << "End of stream reached for" << source->m_sourceId;
    source->handleReconnection();
}

gboolean NetworkSource::onBusMessage(GstBus *bus, GstMessage *message, gpointer user_data)
{
    Q_UNUSED(bus)
    NetworkSource *source = static_cast<NetworkSource*>(user_data);

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError *error;
        gchar *debug;
        gst_message_parse_error(message, &error, &debug);

        QString errorMsg = QString("GStreamer error: %1").arg(error->message);
        qCritical() << "Error in" << source->m_sourceId << ":" << errorMsg;
        if (debug) {
            qDebug() << "Debug info for" << source->m_sourceId << ":" << debug;
        }

        emit source->error(source->sourceType(), source->sourceId(), errorMsg);
        source->handleReconnection();

        g_error_free(error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError *error;
        gchar *debug;
        gst_message_parse_warning(message, &error, &debug);

        qWarning() << "GStreamer warning in" << source->m_sourceId << ":" << error->message;
        if (debug) {
            qDebug() << "Debug info for" << source->m_sourceId << ":" << debug;
        }

        g_error_free(error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream for" << source->m_sourceId;
        source->handleReconnection();
        break;
    default:
        break;
    }

    return TRUE;
}

void NetworkSource::onPadAdded(GstElement *element, GstPad *pad, gpointer user_data)
{
    Q_UNUSED(element)
    NetworkSource *source = static_cast<NetworkSource*>(user_data);

    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) {
        qWarning() << "No caps available on pad for" << source->m_sourceId;
        return;
    }

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(structure);

    qDebug() << "Pad added for" << source->m_sourceId << "with caps:" << gst_caps_to_string(caps);

    if (g_str_has_prefix(name, "audio/")) {
        qDebug() << "Audio pad detected for" << source->m_sourceId << ", analyzing format...";

        // DETECTAR Y ACTUALIZAR FORMATO
        source->updateFormatFromCaps(caps);

        // CONFIGURAR APPSINK CON FORMATO DETECTADO
        source->configureAppSinkWithDetectedFormat();

        // ENLAZAR PAD
        GstPad *sinkpad = gst_element_get_static_pad(source->m_audioconvert, "sink");
        if (sinkpad && !gst_pad_is_linked(sinkpad)) {
            GstPadLinkReturn linkResult = gst_pad_link(pad, sinkpad);
            if (linkResult != GST_PAD_LINK_OK) {
                qWarning() << "Failed to link audio pad for" << source->m_sourceId << ", error:" << linkResult;
            } else {
                qDebug() << "Audio pad linked successfully for" << source->m_sourceId << "with auto-detected format";
                emit source->formatDetected(source->sourceType(), source->sourceId(), source->m_format);
            }
        }
        if (sinkpad) gst_object_unref(sinkpad);
    } else {
        qDebug() << "Non-audio pad ignored for" << source->m_sourceId << ":" << name;
    }

    gst_caps_unref(caps);
}

void NetworkSource::updateFormatFromCaps(GstCaps *caps)
{
    if (!caps) return;

    QMutexLocker locker(&m_formatMutex);

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    if (!structure) return;

    // Detectar sample rate
    gint rate = 44100;  // default
    if (gst_structure_get_int(structure, "rate", &rate)) {
        m_detectedFormat.setSampleRate(rate);
        qDebug() << "Detected sample rate for" << m_sourceId << ":" << rate;
    }

    // Detectar canales
    gint channels = 2;  // default
    if (gst_structure_get_int(structure, "channels", &channels)) {
        m_detectedFormat.setChannelCount(channels);
        qDebug() << "Detected channels for" << m_sourceId << ":" << channels;
    }

    // Detectar formato
    const gchar *format = gst_structure_get_string(structure, "format");
    if (format) {
        QString qtFormat = gstFormatToQtFormat(format);
        if (!qtFormat.isEmpty()) {
            if (qtFormat == "Int16") {
                m_detectedFormat.setSampleFormat(QAudioFormat::Int16);
            } else if (qtFormat == "Int32") {
                m_detectedFormat.setSampleFormat(QAudioFormat::Int32);
            } else if (qtFormat == "Float") {
                m_detectedFormat.setSampleFormat(QAudioFormat::Float);
            }
            qDebug() << "Detected format for" << m_sourceId << ":" << format << "-> Qt format:" << qtFormat;
        }
    }

    // Actualizar formato principal
    m_format = m_detectedFormat;
    m_formatDetected = true;

    qDebug() << "Final detected format for" << m_sourceId << ":"
             << "Rate:" << m_format.sampleRate()
             << "Channels:" << m_format.channelCount()
             << "Format:" << m_format.sampleFormat();
}

void NetworkSource::configureAppSinkWithDetectedFormat()
{
    if (!m_appsink || !m_formatDetected) return;

    // Convertir formato Qt a GStreamer
    QString gstFormat = "S16LE";  // default
    switch (m_format.sampleFormat()) {
    case QAudioFormat::Int16:
        gstFormat = "S16LE";
        break;
    case QAudioFormat::Int32:
        gstFormat = "S32LE";
        break;
    case QAudioFormat::Float:
        gstFormat = "F32LE";
        break;
    default:
        gstFormat = "S16LE";
        break;
    }

    // Crear caps con formato detectado
    GstCaps *caps = gst_caps_new_simple("audio/x-raw",
                                        "format", G_TYPE_STRING, gstFormat.toUtf8().constData(),
                                        "rate", G_TYPE_INT, m_format.sampleRate(),
                                        "channels", G_TYPE_INT, m_format.channelCount(),
                                        nullptr);

    g_object_set(m_appsink, "caps", caps, nullptr);
    gst_caps_unref(caps);

    qDebug() << "AppSink configured for" << m_sourceId << "with detected format:" << gstFormat
             << m_format.sampleRate() << "Hz," << m_format.channelCount() << "ch";
}

QString NetworkSource::gstFormatToQtFormat(const gchar* gstFormat)
{
    if (!gstFormat) return QString();

    QString format(gstFormat);

    if (format.startsWith("S16")) return "Int16";
    if (format.startsWith("S32")) return "Int32";
    if (format.startsWith("F32")) return "Float";
    if (format.startsWith("F64")) return "Float";

    return "Int16";
}
