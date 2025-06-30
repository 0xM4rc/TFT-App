#ifndef NETWORK_RECEIVER_H
#define NETWORK_RECEIVER_H

#include "ireceiver.h"
#include <QTimer>
#include <QAudioFormat>
#include <gst/gstelement.h>
#include <gst/gstbus.h>
#include <gst/app/gstappsink.h>

class NetworkReceiver : public IReceiver
{
    Q_OBJECT

public:
    explicit NetworkReceiver(QObject* parent = nullptr);
    ~NetworkReceiver() override;

    void setUrl(const QString& url);
    void start() override;
    void stop() override;

signals:
    void audioFormatDetected(const QAudioFormat& format);
    void floatChunkReady(const QVector<float>& data, qint64 timestamp);
    void streamFinished();
    void errorOccurred(const QString& error);

private slots:
    void processBusMessages();

private:
    // Métodos de instancia
    GstFlowReturn handleNewSample(GstAppSink* appsink);
    void handleEos();
    gboolean handleBusMessage(GstMessage* msg);
    void cleanup();
    QString createPipelineString(const QString& url);

    // Callbacks estáticos (NUEVOS)
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user);
    static void onEos(GstAppSink* sink, gpointer user);
    static gboolean onBusMessage(GstBus* bus, GstMessage* msg, gpointer user);
    static void onPadAdded(GstElement* element, GstPad* pad, gpointer user);  // NUEVO

    // Miembros privados
    QString m_url;
    bool m_isRunning = false;

    // GStreamer elements
    GstElement* m_pipeline = nullptr;
    GstAppSink* m_appsink = nullptr;
    GstBus* m_bus = nullptr;

    // Qt components
    QTimer* m_busTimer = nullptr;
};

#endif // NETWORK_RECEIVER_H
