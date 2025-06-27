#ifndef IRECEIVER_H
#define IRECEIVER_H

#include <QObject>
#include <QByteArray>

class IReceiver : public QObject {
    Q_OBJECT
public:
    explicit IReceiver(QObject* parent = nullptr) : QObject(parent) {}
    ~IReceiver() override {}

public slots:
    virtual void start() = 0;
    virtual void stop()  = 0;

signals:
    void chunkReady(const QVector<float>& samples, qint64 timestamp);
};

#endif // IRECEIVER_H
