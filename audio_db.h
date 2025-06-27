#ifndef AUDIO_DB_H
#define AUDIO_DB_H

#include <QObject>
#include <QSqlDatabase>
#include <QByteArray>

class AudioDb : public QObject {
    Q_OBJECT
public:
    explicit AudioDb(const QString& path, QObject* parent = nullptr);
    ~AudioDb();

    bool insertBlock(int blockIndex, qint64 sampleOffset, const QByteArray& pcm);
    bool insertPeak(int blockIndex, qint64 sampleOffset, float minVal, float maxVal);

    // Recupera min/max para todos los bloques en el rango dado
    QVector<QPair<float,float>> peaksInRange(qint64 offsetStart, qint64 offsetEnd);

private:
    QSqlDatabase m_db;
};

#endif // AUDIO_DB_H
