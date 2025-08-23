#include "receivers/network_receiver.h"
#include "config/audio_configs.h"
#include <QDebug>
#include <QDateTime>

NetworkReceiver::NetworkReceiver(QObject* parent)
    : IReceiver(parent)
{
    static bool gstInitialized = false;
    if (!gstInitialized) {
        gst_init(nullptr, nullptr);
        gstInitialized = true;
    }

    m_busTimer = new QTimer(this);
    // Usar configuración por defecto inicialmente
    m_busTimer->setInterval(m_config.busTimerInterval);
    connect(m_busTimer, &QTimer::timeout,
            this,       &NetworkReceiver::processBusMessages);
}

NetworkReceiver::~NetworkReceiver() {
    stop();
}

bool NetworkReceiver::setConfig(const IReceiverConfig& cfg)
{
    // 1) Comprobar tipo
    const auto* net = dynamic_cast<const NetworkInputConfig*>(&cfg);
    if (!net) {
        qWarning() << "Config incompatible: se esperaba NetworkInputConfig";
        emit errorOccurred(QStringLiteral("Config incompatible: se esperaba NetworkInputConfig"));
        return false;
    }

    // 2) (Opcional pero recomendado) No permitir cambiar mientras corre
    if (m_isRunning) {
        qWarning() << "No se puede cambiar configuración mientras el receptor de red está activo";
        emit errorOccurred(QStringLiteral("No se puede cambiar configuración mientras el receptor de red está activo"));
        return false;
    }

    // 3) Validar con copia para permitir ajustes automáticos sin const_cast
    NetworkInputConfig working = *net;        // copia que puede ajustarse
    auto validation = working.validate(true); // true => permitir ajustes

    if (!validation.ok) {
        qCritical() << "Configuración inválida:";
        for (const QString& error : validation.errors) {
            qCritical() << "  Error:" << error;
        }
        emit errorOccurred(QStringLiteral("Configuración inválida (ver logs)"));
        return false;
    }

    // 4) Warnings/ajustes
    if (!validation.warnings.isEmpty()) {
        qWarning() << "Advertencias de configuración:";
        for (const QString& warning : validation.warnings) {
            qWarning() << "  " << warning;
        }
    }
    if (validation.adjusted) {
        qDebug() << "Configuración ajustada automáticamente";
    }

    // 5) Asignar config (ya ajustada) y aplicar efectos secundarios
    m_config = std::move(working);

    if (m_busTimer) {
        m_busTimer->setInterval(m_config.busTimerInterval);
    } else {
        qWarning() << "m_busTimer es null; no se pudo aplicar el intervalo del bus";
    }

    if (m_config.enableDebugOutput) {
        qDebug() << "Nueva configuración aplicada:";
        qDebug() << "  URL:" << m_config.url;
        qDebug() << "  Max buffers:" << m_config.maxBuffers;
        qDebug() << "  Bus timer interval:" << m_config.busTimerInterval;
        qDebug() << "  Target sample rate:" << m_config.targetSampleRate;
        qDebug() << "  Target channels:" << m_config.targetChannels;
    }

    return true;
}

void NetworkReceiver::start() {
    if (m_config.url.isEmpty()) {
        qWarning() << "URL no especificada en configuración";
        return;
    }
    if (m_isRunning) {
        qWarning() << "NetworkReceiver ya en marcha";
        return;
    }

    // Usar el método del config para generar el pipeline
    QString pipelineStr = m_config.getPipelineString();

    // temporary
    qDebug() << "[NetworkReceiver] URL configurada:" << m_config.url;

    if (m_config.enableDebugOutput) {
        qDebug() << "Pipeline GStreamer:" << pipelineStr;
    }

    // Crear pipeline
    GError* err = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &err);
    if (!m_pipeline || err) {
        qCritical() << "Error al crear pipeline:"
                    << (err ? err->message : "desconocido");
        if (err) g_error_free(err);
        return;
    }

    // Obtener appsink y configurar callbacks
    m_appsink = GST_APP_SINK(gst_bin_get_by_name(GST_BIN(m_pipeline), "sink"));
    if (!m_appsink) {
        qCritical() << "No se pudo obtener el appsink";
        cleanup();
        return;
    }

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewSample;
    callbacks.eos        = onEos;
    gst_app_sink_set_callbacks(m_appsink, &callbacks, this, nullptr);

    // Bus y decodebin pad-added
    m_bus = gst_element_get_bus(m_pipeline);
    GstElement* decoder = gst_bin_get_by_name(GST_BIN(m_pipeline), "decoder");
    if (decoder) {
        g_signal_connect(decoder, "pad-added", G_CALLBACK(onPadAdded), this);
        gst_object_unref(decoder);
    }

    // PLAYING
    if (gst_element_set_state(m_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        qCritical() << "No se pudo cambiar el pipeline a PLAYING";
        cleanup();
        return;
    }

    // Iniciar timer y marcar running
    m_busTimer->start();
    m_isRunning = true;

    if (m_config.enableDebugOutput) {
        qDebug() << "NetworkReceiver arrancado correctamente";
    }
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

QString NetworkReceiver::createPipelineString() {
    // Ahora delegamos al método del config
    return m_config.getPipelineString();
}

void NetworkReceiver::cleanup() {
    if (m_appsink) {
        // "anulamos" los callbacks de forma válida
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
    if (!m_isRunning)
        return GST_FLOW_FLUSHING;

    // 1) Pull the sample
    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (!sample)
        return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps*   caps   = gst_sample_get_caps(sample);
    if (!buffer || !caps) {
        if (buffer) gst_buffer_unref(buffer);
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // 2) Extract PTS (in nanoseconds)
    GstClockTime pts = GST_BUFFER_PTS(buffer);
    guint64 timestampNs;
    if (pts != GST_CLOCK_TIME_NONE) {
        timestampNs = pts;
    } else {
        timestampNs = static_cast<guint64>(QDateTime::currentMSecsSinceEpoch()) * 1'000'000ULL;
    }

    // 3) Read the real format from caps
    GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar*  fmtStr    = gst_structure_get_string(structure, "format");
    int           channels  = 0;
    int           rate      = 0;
    gst_structure_get_int(structure, "channels", &channels);
    gst_structure_get_int(structure, "rate", &rate);

    // 4) Emit audioFormatDetected once
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
            fmt.setSampleFormat(QAudioFormat::Float);
        }
        emit audioFormatDetected(fmt);
        emittedFmt = true;
    }

    // 5) Get buffer duration
    GstClockTime duration = GST_BUFFER_DURATION(buffer);

    // 6) Map buffer and compute sample count
    GstMapInfo info;
    if (!gst_buffer_map(buffer, &info, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }
    int sampleSize = (QString(fmtStr) == "S16LE" ? sizeof(qint16) : sizeof(float));
    int sampleCount = info.size / sampleSize;

    // 7) Compute duration in seconds and arrival frequency
    double durSec;
    if (duration != GST_CLOCK_TIME_NONE) {
        durSec = double(duration) / GST_SECOND;
    } else {
        durSec = double(sampleCount) / double(rate);
    }
    double freqHz = 1.0 / durSec;

    // 8) Debug output: usar configuración para decidir si mostrar
    if (m_config.logBufferStats) {
        qDebug() << QString("Buffer de %1 muestras a %2 Hz → %.3f s → freq ≈ %.1f Hz")
                        .arg(sampleCount)
                        .arg(rate)
                        .arg(durSec, 0, 'f', 3)
                        .arg(freqHz, 0, 'f', 1);
    }

    // 9) Convert to floats
    QVector<float> floats(sampleCount);
    if (QString(fmtStr) == "S16LE") {
        const qint16* ptr = reinterpret_cast<const qint16*>(info.data);
        for (int i = 0; i < sampleCount; ++i) {
            floats[i] = ptr[i] / 32768.0f;
        }
    } else {
        const float* ptr = reinterpret_cast<const float*>(info.data);
        for (int i = 0; i < sampleCount; ++i) {
            floats[i] = ptr[i];
        }
    }

    gst_buffer_unmap(buffer, &info);
    gst_sample_unref(sample);

    // 10) Emit floats + nanosecond timestamp on the main thread
    QMetaObject::invokeMethod(this, [this, floats = std::move(floats), timestampNs]() {
        if (m_isRunning)
            emit floatChunkReady(floats, timestampNs);
    }, Qt::QueuedConnection);

    return GST_FLOW_OK;
}

void NetworkReceiver::handleEos() {
    if (m_config.enableDebugOutput) {
        qDebug() << "End of stream";
    }
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
        if (!debugMsg.isEmpty() && m_config.enableDebugOutput) {
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
        if (m_config.enableDebugOutput) {
            qDebug() << "End of stream alcanzado";
        }
        handleEos();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        if (m_config.enableDebugOutput) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(m_pipeline)) {
                qDebug() << "Pipeline cambió de estado:"
                         << gst_element_state_get_name(old_state) << "->"
                         << gst_element_state_get_name(new_state);
            }
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
    // added temporary
    gchar* s = caps ? gst_caps_to_string(caps) : g_strdup("no caps");
    qDebug() << "[pad-added]" << s;
    g_free(s);

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
