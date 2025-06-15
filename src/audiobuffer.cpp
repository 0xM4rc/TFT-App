#include "include/audiobuffer.h"
#include <QDebug>

AudioBuffer::AudioBuffer(QObject *parent)
    : QObject(parent)
    , m_maxSize(1000) // Default: 1000 chunks
    , m_totalBytes(0)
{
}

void AudioBuffer::addChunk(const AudioChunk &chunk)
{
    QMutexLocker locker(&m_mutex);

    m_chunks.enqueue(chunk);
    m_totalBytes += chunk.data.size();

    trimToSize();

    emit chunkAdded(chunk);

    if (m_chunks.size() >= m_maxSize) {
        emit bufferFull();
    }
}

AudioChunk AudioBuffer::getChunk()
{
    QMutexLocker locker(&m_mutex);

    if (m_chunks.isEmpty()) {
        return AudioChunk(QByteArray(), QString(), QAudioFormat());
    }

    AudioChunk chunk = m_chunks.dequeue();
    m_totalBytes -= chunk.data.size();

    if (m_chunks.isEmpty()) {
        emit bufferEmpty();
    }

    return chunk;
}

QList<AudioChunk> AudioBuffer::getChunks(int count)
{
    QMutexLocker locker(&m_mutex);

    QList<AudioChunk> result;
    int actualCount = qMin(count, m_chunks.size());

    for (int i = 0; i < actualCount; ++i) {
        AudioChunk chunk = m_chunks.dequeue();
        m_totalBytes -= chunk.data.size();
        result.append(chunk);
    }

    if (m_chunks.isEmpty()) {
        emit bufferEmpty();
    }

    return result;
}

void AudioBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_chunks.clear();
    m_totalBytes = 0;
    emit bufferEmpty();
}

int AudioBuffer::size() const
{
    QMutexLocker locker(&m_mutex);
    return m_chunks.size();
}

bool AudioBuffer::isEmpty() const
{
    QMutexLocker locker(&m_mutex);
    return m_chunks.isEmpty();
}

void AudioBuffer::setMaxSize(int maxSize)
{
    QMutexLocker locker(&m_mutex);
    m_maxSize = maxSize;
    trimToSize();
}

QByteArray AudioBuffer::getLastSeconds(int seconds, const QString &sourceId)
{
    QMutexLocker locker(&m_mutex);

    QByteArray result;
    QDateTime cutoff = QDateTime::currentDateTime().addSecs(-seconds);

    for (const AudioChunk &chunk : m_chunks) {
        if (chunk.timestamp >= cutoff) {
            if (sourceId.isEmpty() || chunk.sourceId == sourceId) {
                result.append(chunk.data);
            }
        }
    }

    return result;
}

QList<AudioChunk> AudioBuffer::getChunksByTimeRange(const QDateTime &start, const QDateTime &end)
{
    QMutexLocker locker(&m_mutex);

    QList<AudioChunk> result;

    for (const AudioChunk &chunk : m_chunks) {
        if (chunk.timestamp >= start && chunk.timestamp <= end) {
            result.append(chunk);
        }
    }

    return result;
}

void AudioBuffer::trimToSize()
{
    while (m_chunks.size() > m_maxSize) {
        AudioChunk removed = m_chunks.dequeue();
        m_totalBytes -= removed.data.size();
    }
}
