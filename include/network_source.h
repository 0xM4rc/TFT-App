#ifndef NETWORK_SOURCE_H
#define NETWORK_SOURCE_H

#include "include/interfaces/audio_source.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QMutex>
#include <QThread>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class NetworkSource : public AudioSource
{
    Q_OBJECT

public:
    explicit NetworkSource(QObject *parent = nullptr);
    ~NetworkSource() override;

    // AudioSource interface
    void start() override;
    void stop() override;
    bool isActive() const override;
    QByteArray getData() override;
    QAudioFormat format() const override;
    QString sourceName() const override;

    // NetworkSource specific methods
    void setUrl(const QUrl &url);
    void setStreamFormat(const QAudioFormat &format);

private slots:
    void reconnectTimer();
    void checkStreamHealth();

private:
    // GStreamer methods
    void initializeGStreamer();
    void createPipeline();
    void destroyPipeline();
    void handleReconnection();

    // Static callbacks for GStreamer
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer user_data);
    static void onEos(GstAppSink *sink, gpointer user_data);
    static gboolean onBusMessage(GstBus *bus, GstMessage *message, gpointer user_data);
    static void onPadAdded(GstElement *element, GstPad *pad, gpointer user_data);

    // GStreamer objects
    GstElement *m_pipeline;
    GstElement *m_source;
    GstElement *m_audioconvert;
    GstElement *m_audioresample;
    GstElement *m_appsink;
    GstBus *m_bus;
    guint m_busWatchId;

    // Stream management
    QUrl m_streamUrl;
    QByteArray m_buffer;
    QMutex m_bufferMutex;

    // Reconnection logic
    QTimer *m_reconnectTimer;
    QTimer *m_healthCheckTimer;
    int m_reconnectAttempts;
    static const int MAX_RECONNECT_ATTEMPTS = 5;

    // Stream state
    bool m_gstreamerInitialized;
    bool m_pipelineCreated;
    qint64 m_lastDataTime;
    static const int HEALTH_CHECK_INTERVAL = 5000; // 5 seconds
    static const int STREAM_TIMEOUT = 10000; // 10 seconds without data
};

#endif // NETWORK_SOURCE_H
