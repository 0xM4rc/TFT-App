#ifndef IRECEIVER_H
#define IRECEIVER_H

#include <QObject>
#include <QVector>
#include <QDateTime>
#include <QAudioFormat>
#include <QString>

// Configuración genérica
struct ReceiverConfig {
    enum Type { Network, Audio } type;
    QString url;              // Para Network
    // Para Audio
    int sampleRate    = 44100;
    int channelCount  = 1;
    QString deviceId;
    bool usePreferred = false;
};

class IReceiver : public QObject {
    Q_OBJECT
public:
    explicit IReceiver(QObject* parent = nullptr) : QObject(parent) {}
    ~IReceiver() override {}

    // Configuración
    virtual void setConfig(const ReceiverConfig& cfg) = 0;
    // Control de flujo
public slots:
    virtual void start() = 0;
    virtual void stop()  = 0;

signals:
    // Datos en punto común
    void floatChunkReady(const QVector<float>& floats, quint64 timestampNs);

    // Señales auxiliares opcionales
    void audioFormatDetected(const QAudioFormat& format);
    void errorOccurred(const QString& error);
    void finished();         // en vez de streamFinished
};

#endif // IRECEIVER_H
