#ifndef NETWORK_RECEIVER_H
#define NETWORK_RECEIVER_H

#include "config/audio_configs.h"
#include "ireceiver.h"
#include <QTimer>
#include <QAudioFormat>
#include <gst/gstelement.h>
#include <gst/gstbus.h>
#include <gst/app/gstappsink.h>

class NetworkReceiver : public IReceiver {
    Q_OBJECT
public:
    explicit NetworkReceiver(QObject* parent = nullptr);
    ~NetworkReceiver() override;

    // Nuevo método para configuración específica
    void setConfig(const NetworkInputConfig& config);

    // Mantener compatibilidad con interfaz existente
    void setUrl(const QString& url) {
        m_config.url = url;
    }

    void start() override;
    void stop() override;

signals:
    void floatChunkReady(const QVector<float>& floats, quint64 timestampNs);
    void streamFinished();

private slots:
    void processBusMessages();

private:
    // Métodos de instancia
    GstFlowReturn handleNewSample(GstAppSink* appsink);
    void handleEos();
    gboolean handleBusMessage(GstMessage* msg);
    void cleanup();
    QString createPipelineString();  // Ya no necesita parámetro

    // Callbacks estáticos
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user);
    static void onEos(GstAppSink* sink, gpointer user);
    static gboolean onBusMessage(GstBus* bus, GstMessage* msg, gpointer user);
    static void onPadAdded(GstElement* element, GstPad* pad, gpointer user);

    // Miembros privados
    NetworkInputConfig m_config;  // Usar el config struct
    bool m_isRunning = false;

    // GStreamer elements
    GstElement* m_pipeline = nullptr;
    GstAppSink* m_appsink = nullptr;
    GstBus* m_bus = nullptr;

    // Qt components
    QTimer* m_busTimer = nullptr;
};

#endif // NETWORK_RECEIVER_H
