#include "network_receiver.h"
#include <QDebug>
#include <QDateTime>
#include <QTimer>
#include <QThread>

NetworkReceiver::NetworkReceiver(QObject* parent)
    : IReceiver(parent)
    , m_pipeline(nullptr)
    , m_appsink(nullptr)
    , m_bus(nullptr)
    , m_isRunning(false)
    , m_busWatchId(0)
{
    // Inicializar GStreamer solo una vez
    static bool gstInitialized = false;
    if (!gstInitialized) {
        gst_init(nullptr, nullptr);
        gstInitialized = true;
    }

    // Timer para procesar mensajes del bus en el hilo principal
    m_busTimer = new QTimer(this);
    connect(m_busTimer, &QTimer::timeout, this, &NetworkReceiver::processBusMessages);
}

NetworkReceiver::~NetworkReceiver() {
    stop();
}

void NetworkReceiver::setUrl(const QString& url) {
    m_url = url;
}

void NetworkReceiver::start() {
    if (m_url.isEmpty()) {
        qWarning() << "URL no especificada. Use setUrl() antes de start()";
        return;
    }

    if (m_isRunning) {
        qWarning() << "NetworkReceiver ya está ejecutándose";
        return;
    }

    // Crear pipeline basado en el tipo de URL
    QString pipelineStr = createPipelineString(m_url);
    qDebug() << "Pipeline string:" << pipelineStr;

    GError* error = nullptr;
    m_pipeline = gst_parse_launch(pipelineStr.toUtf8().constData(), &error);

    if (!m_pipeline || error) {
        qCritical() << "Error creando pipeline:" << (error ? error->message : "Unknown error");
        if (error) g_error_free(error);
        return;
    }

    // Obtener el elemento appsink
    m_appsink = gst_bin_get_by_name(GST_BIN(m_pipeline), "sink");
    if (!m_appsink) {
        qCritical() << "No se pudo obtener el elemento appsink";
        cleanup();
        return;
    }

    // Configurar propiedades del appsink para mayor estabilidad
    g_object_set(m_appsink,
                 "emit-signals", TRUE,
                 "sync", FALSE,
                 "max-buffers", 100,
                 "drop", TRUE,
                 nullptr);

    // Configurar callbacks para appsink de forma más segura
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = onNewSample;
    callbacks.eos = onEos;
    callbacks.new_preroll = nullptr;

    gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), &callbacks, this, nullptr);

    // Configurar el bus para mensajes (sin watch automático)
    m_bus = gst_element_get_bus(m_pipeline);

    // Iniciar el pipeline
    GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qCritical() << "Error iniciando pipeline";
        cleanup();
        return;
    }

    // Iniciar el timer para procesar mensajes del bus
    m_busTimer->start(50); // Revisar cada 50ms
    m_isRunning = true;

    qDebug() << "Pipeline iniciado correctamente para URL:" << m_url;
}

void NetworkReceiver::stop() {
    if (!m_isRunning) {
        return;
    }

    m_isRunning = false;

    // Detener el timer
    if (m_busTimer) {
        m_busTimer->stop();
    }

    // Cambiar estado del pipeline de forma segura
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);

        // Esperar a que el estado cambie
        GstState state;
        GstStateChangeReturn ret = gst_element_get_state(m_pipeline, &state, nullptr, GST_CLOCK_TIME_NONE);
        if (ret == GST_STATE_CHANGE_FAILURE) {
            qWarning() << "Error cambiando estado del pipeline a NULL";
        }
    }

    cleanup();
}

QString NetworkReceiver::createPipelineString(const QString& url) {
    QString pipeline;

    // Elementos de conversión de audio básicos
    QString audioConvert = " ! audioconvert ! audioresample";

    if (url.startsWith("udp://")) {
        // Para UDP: udpsrc -> decoder -> audioconvert -> appsink
        QString host = "0.0.0.0";
        int port = 1234;

        QString urlPart = url.mid(6);
        QStringList parts = urlPart.split(":");
        if (parts.size() >= 1 && !parts[0].isEmpty()) {
            host = parts[0];
        }
        if (parts.size() >= 2) {
            port = parts[1].toInt();
        }

        pipeline = QString("udpsrc address=%1 port=%2 ! decodebin")
                       .arg(host).arg(port) + audioConvert + " ! appsink name=sink";

    } else if (url.startsWith("tcp://")) {
        // Para TCP: tcpclientsrc -> decoder -> audioconvert -> appsink
        QString host = "localhost";
        int port = 1234;

        QString urlPart = url.mid(6);
        QStringList parts = urlPart.split(":");
        if (parts.size() >= 1 && !parts[0].isEmpty()) {
            host = parts[0];
        }
        if (parts.size() >= 2) {
            port = parts[1].toInt();
        }

        pipeline = QString("tcpclientsrc host=%1 port=%2 ! decodebin")
                       .arg(host).arg(port) + audioConvert + " ! appsink name=sink";

    } else if (url.startsWith("http://") || url.startsWith("https://")) {
        // Para HTTP/HTTPS: souphttpsrc -> decoder -> audioconvert -> appsink
        pipeline = QString("souphttpsrc location=%1 ! decodebin")
                       .arg(url) + audioConvert + " ! appsink name=sink";

    } else if (url.startsWith("rtsp://")) {
        // Para RTSP: rtspsrc -> decoder -> audioconvert -> appsink
        pipeline = QString("rtspsrc location=%1 ! decodebin")
                       .arg(url) + audioConvert + " ! appsink name=sink";

    } else if (url.startsWith("file://")) {
        // Para archivo local: filesrc -> decoder -> audioconvert -> appsink
        QString filePath = url.mid(7);
        pipeline = QString("filesrc location=%1 ! decodebin")
                       .arg(filePath) + audioConvert + " ! appsink name=sink";

    } else {
        // Pipeline genérico
        pipeline = QString("souphttpsrc location=%1 ! decodebin")
                       .arg(url) + audioConvert + " ! appsink name=sink";
    }

    return pipeline;
}

void NetworkReceiver::cleanup() {
    // Limpiar callbacks del appsink antes de liberar
    if (m_appsink) {
        gst_app_sink_set_callbacks(GST_APP_SINK(m_appsink), nullptr, nullptr, nullptr);
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
    if (!m_bus || !m_isRunning) {
        return;
    }

    GstMessage* message;
    while ((message = gst_bus_pop(m_bus)) != nullptr) {
        handleBusMessage(message);
        gst_message_unref(message);
    }
}

// Callback estático para nuevas muestras
GstFlowReturn NetworkReceiver::onNewSample(GstAppSink* appsink, gpointer user_data) {
    NetworkReceiver* receiver = static_cast<NetworkReceiver*>(user_data);
    return receiver->handleNewSample(appsink);
}

// Callback estático para EOS
void NetworkReceiver::onEos(GstAppSink* /*appsink*/, gpointer user_data) {
    NetworkReceiver* receiver = static_cast<NetworkReceiver*>(user_data);
    receiver->handleEos();
}

// Callback estático para mensajes del bus
gboolean NetworkReceiver::onBusMessage(GstBus* /*bus*/, GstMessage* message, gpointer user_data) {
    NetworkReceiver* receiver = static_cast<NetworkReceiver*>(user_data);
    return receiver->handleBusMessage(message);
}

// Implementación del manejo de nuevas muestras (simplificada)
GstFlowReturn NetworkReceiver::handleNewSample(GstAppSink* appsink) {
    if (!m_isRunning) {
        return GST_FLOW_FLUSHING;
    }

    GstSample* sample = gst_app_sink_pull_sample(appsink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    // Mapear el buffer para acceder a los datos
    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // Convertir datos a QByteArray directamente
        QByteArray data(reinterpret_cast<const char*>(map.data), map.size);
        qint64 timestamp = QDateTime::currentMSecsSinceEpoch();

        gst_buffer_unmap(buffer, &map);

        // Emitir la señal de forma thread-safe usando QMetaObject::invokeMethod
        QMetaObject::invokeMethod(this, [this, data, timestamp]() {
            if (m_isRunning) {
                emit chunkReady(data, timestamp);
            }
        }, Qt::QueuedConnection);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void NetworkReceiver::handleEos() {
    qDebug() << "End of stream alcanzado";
    QMetaObject::invokeMethod(this, [this]() {
        if (m_isRunning) {
            emit streamFinished();
        }
    }, Qt::QueuedConnection);
}

gboolean NetworkReceiver::handleBusMessage(GstMessage* message) {
    if (!m_isRunning) {
        return FALSE;
    }

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR: {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);

        QString errorMsg = error ? QString(error->message) : "Unknown error";
        QString debugInfo = debug ? QString(debug) : "";

        qCritical() << "Error de GStreamer:" << errorMsg;
        if (!debugInfo.isEmpty()) {
            qDebug() << "Debug info:" << debugInfo;
        }

        // Emitir señal de error de forma thread-safe
        QMetaObject::invokeMethod(this, [this, errorMsg]() {
            if (m_isRunning) {
                emit errorOccurred(errorMsg);
            }
        }, Qt::QueuedConnection);

        if (error) g_error_free(error);
        if (debug) g_free(debug);
        break;
    }
    case GST_MESSAGE_WARNING: {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_warning(message, &error, &debug);
        qWarning() << "Warning de GStreamer:" << (error ? error->message : "Unknown warning");
        if (debug) {
            qDebug() << "Debug info:" << debug;
            g_free(debug);
        }
        if (error) g_error_free(error);
        break;
    }
    case GST_MESSAGE_EOS:
        qDebug() << "End of stream";
        handleEos();
        break;
    case GST_MESSAGE_STATE_CHANGED: {
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(m_pipeline)) {
            GstState oldState, newState, pendingState;
            gst_message_parse_state_changed(message, &oldState, &newState, &pendingState);
            qDebug() << "Pipeline cambió de estado:" << gst_element_state_get_name(oldState)
                     << "a" << gst_element_state_get_name(newState);
        }
        break;
    }
    default:
        break;
    }

    return TRUE;
}
