#ifndef AUDIOCHUNK_H
#define AUDIOCHUNK_H

#include <QByteArray>
#include <QString>
#include <QAudioFormat>
#include <QDateTime>

struct AudioChunk
{
    QByteArray data;
    QString sourceId;
    QAudioFormat format;
    QDateTime timestamp;

    AudioChunk() : timestamp(QDateTime::currentDateTime()) {}

    AudioChunk(const QByteArray &d, const QString &id, const QAudioFormat &fmt)
        : data(d), sourceId(id), format(fmt), timestamp(QDateTime::currentDateTime()) {}
};

#endif // AUDIOCHUNK_H
