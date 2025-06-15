#ifndef AUDIOBUFFER_H
#define AUDIOBUFFER_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include "audiochunk.h"

class AudioBuffer : public QObject
{
    Q_OBJECT

public:
    explicit AudioBuffer(QObject *parent = nullptr);

    void addChunk(const AudioChunk &chunk);
    AudioChunk getChunk();
    QList<AudioChunk> getChunks(int count);
    void clear();

    int size() const;
    bool isEmpty() const;
    void setMaxSize(int maxSize);

    QByteArray getLastSeconds(int seconds, const QString &sourceId = QString());
    QList<AudioChunk> getChunksByTimeRange(const QDateTime &start, const QDateTime &end);

signals:
    void chunkAdded(const AudioChunk &chunk);
    void bufferFull();
    void bufferEmpty();

private:
    void trimToSize();

    QQueue<AudioChunk> m_chunks;
    mutable QMutex m_mutex;
    int m_maxSize;
    qint64 m_totalBytes;
};

#endif // AUDIOBUFFER_H
