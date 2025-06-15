#ifndef AUDIOPROCESSOR_H
#define AUDIOPROCESSOR_H

#include <QObject>
#include <QSharedPointer>
#include <QTimer>
#include <QThread>
#include <QMutex>
#include <QMutexLocker>
#include <QList>
#include <QAudioFormat>
#include "audiobuffer.h"
#include "interfaces/audio_source.h"

class AudioProcessor : public QObject
{
    Q_OBJECT

public:
    explicit AudioProcessor(QObject *parent = nullptr);
    ~AudioProcessor();

    void addSource(QSharedPointer<AudioSource> source);
    void removeSource(QSharedPointer<AudioSource> source);
    void removeSource(const QString &sourceName);

    void start();
    void stop();
    bool isRunning() const;

    void setProcessingInterval(int milliseconds);
    void setBufferSize(int maxChunks);

    QSharedPointer<AudioBuffer> getBuffer() const;
    QList<QSharedPointer<AudioSource>> getSources() const;

signals:
    void processingStarted();
    void processingStopped();
    void dataProcessed(const AudioChunk &chunk);
    void error(const QString &errorString);

private slots:
    void processAudioData();
    void handleSourceDataReady();
    void handleSourceError(const QString &error);
    void handleSourceStateChanged(bool active);

private:
    QByteArray convertToCommonFormat(const QByteArray &data, const QAudioFormat &sourceFormat);
    QAudioFormat getCommonFormat() const;

    QSharedPointer<AudioBuffer> m_buffer;
    QList<QSharedPointer<AudioSource>> m_sources;
    mutable QMutex m_sourcesMutex;

    QTimer *m_processingTimer;
    QThread *m_processingThread;
    bool m_running;
    int m_processingInterval;
};

#endif // AUDIOPROCESSOR_H
