#ifndef NETWORK_SOURCE_H
#define NETWORK_SOURCE_H

#include "include/interfaces/audio_source.h"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QMutex>
#include <QCryptographicHash>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class NetworkSource : public AudioSource
{
    Q_OBJECT

public:
    explicit NetworkSource(const QUrl& url = QUrl(), QObject *parent = nullptr);
    ~NetworkSource() override;

    // AudioSource interface
    void start() override;
    void stop() override;
    bool isActive() const override;
    QByteArray getData() override;
    QAudioFormat format() const override;

    // Identificación
    SourceType sourceType() const override { return SourceType::Network; }
    QString    sourceId()   const override { return QStringLiteral("Net:%1").arg(m_sourceId); }
    QString    sourceName() const override { return m_sourceName; }

    // NetworkSource specific
    void setUrl(const QUrl &url);
    void setStreamFormat(const QAudioFormat &format);


signals:
    // Señales con type + id + payload
    void formatDetected(SourceType type, const QString& id, const QAudioFormat &format);

private slots:
    void reconnectTimer();
    void checkStreamHealth();

private:
    // GStreamer setup / teardown
    void initializeGStreamer();
    void createPipeline();
    void destroyPipeline();
    void handleReconnection();

    // Auto-detección de formato
    void updateFormatFromCaps(GstCaps *caps);
    void configureAppSinkWithDetectedFormat();
    QString gstFormatToQtFormat(const gchar* gstFormat);

    // Callbacks estáticos para GStreamer
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer user_data);
    static void           onEos       (GstAppSink *sink, gpointer user_data);
    static gboolean       onBusMessage(GstBus *bus, GstMessage *message, gpointer user_data);
    static void           onPadAdded  (GstElement *element, GstPad *pad, gpointer user_data);

    // Elementos GStreamer
    GstElement *m_pipeline      = nullptr;
    GstElement *m_source        = nullptr;
    GstElement *m_audioconvert  = nullptr;
    GstElement *m_audioresample = nullptr;
    GstElement *m_appsink       = nullptr;
    GstBus     *m_bus           = nullptr;
    guint       m_busWatchId    = 0;

    // Gestión de stream
    QUrl       m_streamUrl;
    QByteArray m_buffer;
    QMutex     m_bufferMutex;

    // Reintento automático
    QTimer *m_reconnectTimer;
    QTimer *m_healthCheckTimer;
    int     m_reconnectAttempts    = 0;
    static const int MAX_RECONNECT_ATTEMPTS = 5;

    // Estado del stream
    bool    m_gstreamerInitialized = false;
    bool    m_pipelineCreated      = false;
    qint64  m_lastDataTime         = 0;
    static const int HEALTH_CHECK_INTERVAL = 5000;  // ms
    static const int STREAM_TIMEOUT         = 10000; // ms

    // Auto-detección de formato
    QAudioFormat m_format;          // formato fallback
    QAudioFormat m_detectedFormat;
    bool         m_formatDetected   = false;
    QMutex       m_formatMutex;

    // Identificadores
    QString m_sourceId;             // MD5 hash de la URL
    QString m_sourceName;           // versión friendly de la URL
};

#endif // NETWORK_SOURCE_H
