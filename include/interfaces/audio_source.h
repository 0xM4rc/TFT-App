#ifndef AUDIOSOURCE_H
#define AUDIOSOURCE_H

#include <QObject>
#include <QByteArray>
#include <QAudioFormat>

enum class SourceType { Microphone, Network}; // Identificador tipo de interfaz

class AudioSource : public QObject
{
    Q_OBJECT

public:
    explicit AudioSource(QObject *parent = nullptr);
    virtual ~AudioSource() = default;

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isActive() const = 0;
    virtual QByteArray getData() = 0;
    virtual QAudioFormat format() const = 0;
    virtual QString sourceName() const = 0;

    // Identificadores de interfaz
    virtual SourceType sourceType() const = 0;
    virtual QString    sourceId()   const = 0;
signals:
    void dataReady(SourceType type, const QString& id);
    void stateChanged(SourceType type, const QString& id, bool active);
    void error(SourceType type, const QString& id, const QString& msg);


protected:
    QAudioFormat m_format;
    bool m_active;
};

#endif // AUDIOSOURCE_H
