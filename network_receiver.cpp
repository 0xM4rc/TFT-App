#include "network_receiver.h"
#include <QDebug>
#include <QDateTime>
#include <cstring>

NetworkReceiver::NetworkReceiver(QObject* parent)
    : IReceiver(parent)
{
    static bool gstInitialized = false;
    if (!gstInitialized) {
        gst_init(nullptr, nullptr);
        gstInitialized = true;
    }

    m_busTimer = new QTimer(this);
    m_busTimer->setInterval(50);
    connect(m_busTimer, &QTimer::timeout,
            this,       &NetworkReceiver::processBusMessages);
}

NetworkReceiver::~NetworkReceiver() {
    stop();
}

void NetworkReceiver::setUrl(const QString& url) {
    m_url = url;
}

void NetworkReceiver::start() {
    if (m_url.isEmpty()) {
        qWarning() << "URL no especificada";
        return;
    }
    if (m_isRunning) {
        qWarning() << "NetworkReceiver ya en marcha";
        return;
    }

    // 1) Pipeline sin caps fijas: dejaremos que decodebin entregue cualquier raw audio
    QString pipelineStr = QString(
                              "souphttpsrc location=%1 ! "
                              "decodebin name=decoder ! "
                              "audioconvert ! audioresample ! "
                              "appsink name=sink emit-signals=true sync=false max-buffers=10 drop=true"
                              ).arg(m_url);

    qDebug() << "Pipeline GStreamer:" << pipelineStr;

    // 2) Crear pipeline
    GError* err = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &err);
    if (!m_pipeline || err) {
        qCritical() << "Error al crear pipeline:"
                    << (err ? err->message : "desconocido");
        if (err) g_error_free(err);
        return;
    }

    // 3) Obtener appsink y callbacks (igual que antes)
    m_appsink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(m_pipeline), "sink"));
    if (!m_appsink) { qCritical() << "No se pudo obtener el appsink"; cleanup(); return; }
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewSample;
    callbacks.eos        = onEos;
    gst_app_sink_set_callbacks(m_appsink, &callbacks, this, nullptr);

    // 4) Bus y decodebin pad-added (igual que antes)
    m_bus = gst_element_get_bus(m_pipeline);
    GstElement* decoder = gst_bin_get_by_name(GST_BIN(m_pipeline), "decoder");
    if (decoder) {
        g_signal_connect(decoder, "pad-added", G_CALLBACK(onPadAdded), this);
        gst_object_unref(decoder);
    }

    // 5) PLAYING
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        qCritical() << "No se pudo cambiar el pipeline a PLAYING";
        cleanup();
        return;
    }

    // 6) Iniciar timer y marcar running
    m_busTimer->start();
    m_isRunning = true;

    qDebug() << "NetworkReceiver arrancado correctamente";
}

void NetworkReceiver::stop() {
    if (!m_isRunning) return;
    m_isRunning = false;
    m_busTimer->stop();

    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }
    cleanup();
}

QString NetworkReceiver::createPipelineString(const QString& url) {
    // Ya inyectado directamente en start()
    return QString();
}

void NetworkReceiver::cleanup() {
    if (m_appsink) {
        // “anulamos” los callbacks de forma válida
        GstAppSinkCallbacks emptyCb = { nullptr, nullptr, nullptr };
        gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink),
                                   &emptyCb,
                                   nullptr,
                                   nullptr);
        gst_object_unref(m_appsink);
        m_appsink = nullptr;
    }

    if (m_bus) {
        gst_object_unref(m_bus);
        m_bus = nullptr;
    }

    if (m_pipeline) {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }

    m_isRunning = false;
}


void NetworkReceiver::processBusMessages() {
    if (!m_isRunning || !m_bus) return;
    GstMessage* msg = nullptr;
    while ((msg = gst_bus_pop(m_bus)) != nullptr) {
        handleBusMessage(msg);
        gst_message_unref(msg);
    }
}

// ——— Callbacks estáticos ———

GstFlowReturn NetworkReceiver::onNewSample(GstAppSink* sink, gpointer user) {
    return static_cast<NetworkReceiver*>(user)->handleNewSample(sink);
}

void NetworkReceiver::onEos(GstAppSink* /*sink*/, gpointer user) {
    static_cast<NetworkReceiver*>(user)->handleEos();
}

gboolean NetworkReceiver::onBusMessage(GstBus* /*bus*/, GstMessage* msg, gpointer user) {
    return static_cast<NetworkReceiver*>(user)->handleBusMessage(msg);
}

// ——— Implementaciones de instancia ———

GstFlowReturn NetworkReceiver::handleNewSample(GstAppSink* appsink) {
    if (!m_isRunning) return GST_FLOW_FLUSHING;

    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps*   caps   = gst_sample_get_caps(sample);
    if (!buffer || !caps) {
        if (buffer) gst_buffer_unref(buffer);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 1) Leer el formato real de las caps
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar*  fmtStr    = gst_structure_get_string(structure, "format");
    int           channels  = 0;
    int           rate      = 0;
    gst_structure_get_int(structure, "channels", &channels);
    gst_structure_get_int(structure, "rate", &rate);

    // 2) Construir y emitir un QAudioFormat dinámico la primera vez
    static bool emittedFmt = false;
    if (!emittedFmt) {
        QAudioFormat fmt;
        fmt.setSampleRate(rate);
        fmt.setChannelCount(channels);
        if (QString(fmtStr) == "S16LE") {
            fmt.setSampleFormat(QAudioFormat::Int16);
        } else if (QString(fmtStr) == "F32LE") {
            fmt.setSampleFormat(QAudioFormat::Float);
        } else {
            // Otros posibles formatos raw, asume Float
            fmt.setSampleFormat(QAudioFormat::Float);
        }
        emit audioFormatDetected(fmt);
        emittedFmt = true;
    }

    // 3) Mapear buffer y convertir a float
    GstMapInfo info;
    if (!gst_buffer_map(buffer, &info, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    QVector<float> floats;
    int count = info.size / (QString(fmtStr) == "S16LE" ? sizeof(qint16) : sizeof(float));
    floats.resize(count);

    if (QString(fmtStr) == "S16LE") {
        const qint16* ptr = reinterpret_cast<const qint16*>(info.data);
        for (int i = 0; i < count; ++i) {
            floats[i] = ptr[i] / 32768.0f;
        }
    } else {
        const float* ptr = reinterpret_cast<const float*>(info.data);
        for (int i = 0; i < count; ++i) {
            floats[i] = ptr[i];
        }
    }

    gst_buffer_unmap(buffer, &info);
    gst_sample_unref(sample);

    // 4) Emitir en hilo principal
    qint64 timestamp = QDateTime::currentMSecsSinceEpoch();
    QMetaObject::invokeMethod(this, [this, floats, timestamp](){
        if (m_isRunning) emit floatChunkReady(floats, timestamp);
    }, Qt::QueuedConnection);

    return GST_FLOW_OK;
}


void NetworkReceiver::handleEos() {
    qDebug() << "End of stream";
    QMetaObject::invokeMethod(this, [this](){
        if (m_isRunning) emit streamFinished();
    }, Qt::QueuedConnection);
}

gboolean NetworkReceiver::handleBusMessage(GstMessage* msg) {
    if (!m_isRunning) return FALSE;

    switch (GST_MESSAGE_TYPE(msg)) {
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(msg, &err, &debug);

        QString errorMsg = err ? QString::fromUtf8(err->message) : "Error desconocido";
        QString debugMsg = debug ? QString::fromUtf8(debug) : "";

        qCritical() << "GStreamer error:" << errorMsg;
        if (!debugMsg.isEmpty()) {
            qDebug() << "Debug info:" << debugMsg;
        }

        // Limpieza
        if (err) g_error_free(err);
        if (debug) g_free(debug);

        // Emitir error en hilo principal
        QMetaObject::invokeMethod(this, [this, errorMsg](){
            if (m_isRunning) {
                emit errorOccurred(errorMsg);
            }
        }, Qt::QueuedConnection);
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream alcanzado";
        handleEos();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        GstState old_state, new_state, pending_state;
        gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

        if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
            qDebug() << "Pipeline cambió de estado:"
                     << gst_element_state_get_name(old_state) << "->"
                     << gst_element_state_get_name(new_state);
        }
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_warning(msg, &err, &debug);

        qWarning() << "GStreamer warning:"
                   << (err ? err->message : "Warning desconocido");

        if (err) g_error_free(err);
        if (debug) g_free(debug);
        break;
    }
    default:
        break;
    }

    return TRUE;
}

void NetworkReceiver::onPadAdded(GstElement* /*element*/, GstPad* pad, gpointer user) {
    NetworkReceiver* self = static_cast<NetworkReceiver*>(user);

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps) return;

    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* name = gst_structure_get_name(structure);

    // Solo conectar pads de audio
    if (g_str_has_prefix(name, "audio/")) {
        GstElement* audioconvert = gst_bin_get_by_name(GST_BIN(self->m_pipeline), "audioconvert");
        if (audioconvert) {
            GstPad* sink_pad = gst_element_get_static_pad(audioconvert, "sink");
            if (sink_pad && !gst_pad_is_linked(sink_pad)) {
                gst_pad_link(pad, sink_pad);
            }
            if (sink_pad) gst_object_unref(sink_pad);
            gst_object_unref(audioconvert);
        }
    }

    gst_caps_unref(caps);
}
