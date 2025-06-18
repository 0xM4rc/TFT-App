#include "include/network_source.h"
#include <QDebug>
#include <QDateTime>
#include <QCoreApplication>
#include <QCryptographicHash>

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
{
    // setup de id
    // ID: MD5 de la URL
    QByteArray raw = url.toString().toUtf8();
    m_sourceId = QCryptographicHash::hash(raw, QCryptographicHash::Md5).toHex();

    // Para mostrar al usuario, puedes recortar la URL:
    m_sourceName = url.toString(QUrl::RemoveUserInfo | QUrl::RemoveScheme);

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
}

NetworkSource::~NetworkSource()
{
    stop();
    destroyPipeline();

    if (m_gstreamerInitialized) {
        gst_deinit();
    }
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
    qDebug() << "GStreamer initialized successfully";
}

void NetworkSource::createPipeline()
{
    if (!m_gstreamerInitialized || m_pipelineCreated) return;

    // Crear pipeline
    m_pipeline = gst_pipeline_new("audio-network-pipeline");
    if (!m_pipeline) {
        qCritical() << "Failed to create GStreamer pipeline";
        return;
    }

    // Crear elementos
    m_source = gst_element_factory_make("uridecodebin", "source");
    m_audioconvert = gst_element_factory_make("audioconvert", "audioconvert");
    m_audioresample = gst_element_factory_make("audioresample", "audioresample");
    m_appsink = gst_element_factory_make("appsink", "appsink");

    if (!m_source || !m_audioconvert || !m_audioresample || !m_appsink) {
        qCritical() << "Failed to create GStreamer elements";
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
        qCritical() << "Failed to link GStreamer elements";
        destroyPipeline();
        return;
    }

    // Configurar bus
    m_bus = gst_element_get_bus(m_pipeline);
    m_busWatchId = gst_bus_add_watch(m_bus, NetworkSource::onBusMessage, this);

    m_pipelineCreated = true;
    m_formatDetected = false;  // Reset detection flag
    qDebug() << "GStreamer pipeline created successfully";
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

    qDebug() << "GStreamer pipeline destroyed";
}

void NetworkSource::start()
{
    if (m_active || m_streamUrl.isEmpty() || !m_gstreamerInitialized) return;

    createPipeline();
    if (!m_pipelineCreated) return;

    // Configurar URL
    g_object_set(m_source, "uri", m_streamUrl.toString().toUtf8().constData(), nullptr);

    // Iniciar pipeline
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCritical() << "Failed to start GStreamer pipeline";
        destroyPipeline();
        return;
    }

    m_active = true;
    m_reconnectAttempts = 0;
    m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
    m_healthCheckTimer->start();

    emit stateChanged(sourceType(), sourceId(), m_active);
    qDebug() << "Network source started with URL:" << m_streamUrl.toString();
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
    qDebug() << "Network source stopped";
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
        qWarning() << "Cannot change URL while active";
        return;
    }
    m_streamUrl = url;
}

void NetworkSource::setStreamFormat(const QAudioFormat &format)
{
    if (m_active) {
        qWarning() << "Cannot change format while active";
        return;
    }
    m_format = format;
}

void NetworkSource::reconnectTimer()
{
    if (m_active && m_gstreamerInitialized) {
        qDebug() << "Attempting to reconnect...";

        // Recrear pipeline
        destroyPipeline();
        createPipeline();

        if (m_pipelineCreated) {
            g_object_set(m_source, "uri", m_streamUrl.toString().toUtf8().constData(), nullptr);

            GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
            if (ret != GST_STATE_CHANGE_FAILURE) {
                m_lastDataTime = QDateTime::currentMSecsSinceEpoch();
                m_healthCheckTimer->start();
                qDebug() << "Reconnection successful";
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
        qWarning() << "Stream timeout detected, attempting reconnection";
        handleReconnection();
    }
}

void NetworkSource::handleReconnection()
{
    if (m_reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        m_reconnectAttempts++;
        int delay = m_reconnectAttempts * 2000; // Exponential backoff

        qDebug() << "Scheduling reconnection in" << delay << "ms (attempt" << m_reconnectAttempts << ")";

        m_healthCheckTimer->stop();
        if (m_pipeline) {
            gst_element_set_state(m_pipeline, GST_STATE_NULL);
        }

        m_reconnectTimer->start(delay);
    } else {
        qWarning() << "Max reconnection attempts reached";
        stop();
        emit error(sourceType(), sourceId(),"Max reconnection attempts reached");
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
    qDebug() << "End of stream reached";
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
        qCritical() << errorMsg;
        if (debug) {
            qDebug() << "Debug info:" << debug;
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

        qWarning() << "GStreamer warning:" << error->message;
        if (debug) {
            qDebug() << "Debug info:" << debug;
        }

        g_error_free(error);
        g_free(debug);
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
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
        qWarning() << "No caps available on pad";
        return;
    }

    GstStructure *structure = gst_caps_get_structure(caps, 0);
    const gchar *name = gst_structure_get_name(structure);

    qDebug() << "Pad added with caps:" << gst_caps_to_string(caps);

    if (g_str_has_prefix(name, "audio/")) {
        qDebug() << "Audio pad detected, analyzing format...";

        // DETECTAR Y ACTUALIZAR FORMATO
        source->updateFormatFromCaps(caps);

        // CONFIGURAR APPSINK CON FORMATO DETECTADO
        source->configureAppSinkWithDetectedFormat();

        // ENLAZAR PAD
        GstPad *sinkpad = gst_element_get_static_pad(source->m_audioconvert, "sink");
        if (sinkpad && !gst_pad_is_linked(sinkpad)) {
            GstPadLinkReturn linkResult = gst_pad_link(pad, sinkpad);
            if (linkResult != GST_PAD_LINK_OK) {
                qWarning() << "Failed to link audio pad, error:" << linkResult;
            } else {
                qDebug() << "Audio pad linked successfully with auto-detected format";
                emit source->formatDetected(source->sourceType(), source->sourceId(), source->m_format);  // Nueva señal
            }
        }
        if (sinkpad) gst_object_unref(sinkpad);
    } else {
        qDebug() << "Non-audio pad ignored:" << name;
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
        qDebug() << "Detected sample rate:" << rate;
    }

    // Detectar canales
    gint channels = 2;  // default
    if (gst_structure_get_int(structure, "channels", &channels)) {
        m_detectedFormat.setChannelCount(channels);
        qDebug() << "Detected channels:" << channels;
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
            qDebug() << "Detected format:" << format << "-> Qt format:" << qtFormat;
        }
    }

    // Actualizar formato principal
    m_format = m_detectedFormat;
    m_formatDetected = true;

    qDebug() << "Final detected format:"
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

    qDebug() << "AppSink configured with detected format:" << gstFormat
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

    return "Int16";  // default fallback
}
