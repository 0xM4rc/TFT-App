#ifndef NETWORK_RECEIVER_H
#define NETWORK_RECEIVER_H

#include "ireceiver.h"
#include <QObject>
#include <QString>
#include <QByteArray>
#include <QTimer>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class NetworkReceiver : public IReceiver
{
    Q_OBJECT

public:
    explicit NetworkReceiver(QObject* parent = nullptr);
    ~NetworkReceiver() override;

    // Función para establecer la URL de origen
    void setUrl(const QString& url);

    // Métodos heredados de IReceiver
    void start() override;
    void stop() override;

signals:
    // Señal emitida cuando se reciben datos
    void chunkReady(const QByteArray& data, qint64 timestamp);

    // Señales adicionales para manejo de eventos
    void streamFinished();
    void errorOccurred(const QString& error);

private slots:
    void processBusMessages();

private:
    // Métodos privados
    QString createPipelineString(const QString& url);
    void cleanup();

    // Callbacks estáticos de GStreamer
    static GstFlowReturn onNewSample(GstAppSink* appsink, gpointer user_data);
    static void onEos(GstAppSink* appsink, gpointer user_data);
    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer user_data);

    // Métodos de manejo de callbacks
    GstFlowReturn handleNewSample(GstAppSink* appsink);
    void handleEos();
    gboolean handleBusMessage(GstMessage* message);

private:
    // Variables miembro
    QString m_url;
    bool m_isRunning;

    // Elementos de GStreamer
    GstElement* m_pipeline;
    GstElement* m_appsink;
    GstBus* m_bus;
    guint m_busWatchId;

    // Timer para procesar mensajes del bus
    QTimer* m_busTimer;
};

#endif // NETWORK_RECEIVER_H
