#include "audio_db.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

AudioDb::AudioDb(const QString& path, QObject* parent)
    : QObject(parent)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "audio_conn");
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qCritical() << "No se pudo abrir DB:" << m_db.lastError();
        return;
    }
    QSqlQuery q(m_db);
    // Tabla de bloques PCM
    q.exec(R"(
      CREATE TABLE IF NOT EXISTS AudioBlock (
        blockIndex   INTEGER PRIMARY KEY,
        sampleOffset INTEGER,
        samples      BLOB
      )");
    // Tabla de min/max
    q.exec(R"(
      CREATE TABLE IF NOT EXISTS PeakCache (
        blockIndex   INTEGER PRIMARY KEY,
        sampleOffset INTEGER,
        minValue     REAL,
        maxValue     REAL
      )");
}

AudioDb::~AudioDb() {
    m_db.close();
    QSqlDatabase::removeDatabase("audio_conn");
}

bool AudioDb::insertBlock(int blockIndex, qint64 sampleOffset, const QByteArray& pcm) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO AudioBlock(blockIndex, sampleOffset, samples) "
              "VALUES(?, ?, ?)");
    q.addBindValue(blockIndex);
    q.addBindValue(sampleOffset);
    q.addBindValue(pcm);
    if (!q.exec()) {
        qCritical() << "Error insertBlock:" << q.lastError();
        return false;
    }
    return true;
}

bool AudioDb::insertPeak(int blockIndex, qint64 sampleOffset, float minVal, float maxVal) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO PeakCache(blockIndex, sampleOffset, minValue, maxValue) "
              "VALUES(?, ?, ?, ?)");
    q.addBindValue(blockIndex);
    q.addBindValue(sampleOffset);
    q.addBindValue(minVal);
    q.addBindValue(maxVal);
    if (!q.exec()) {
        qCritical() << "Error insertPeak:" << q.lastError();
        return false;
    }
    return true;
}

QVector<QPair<float,float>> AudioDb::peaksInRange(qint64 start, qint64 end) {
    QVector<QPair<float,float>> result;
    QSqlQuery q(m_db);
    q.prepare("SELECT minValue, maxValue FROM PeakCache "
              "WHERE sampleOffset BETWEEN ? AND ? "
              "ORDER BY sampleOffset");
    q.addBindValue(start);
    q.addBindValue(end);
    if (!q.exec()) {
        qCritical() << "Error peaksInRange:" << q.lastError();
        return result;
    }
    while (q.next()) {
        result.append({ q.value(0).toFloat(),
                       q.value(1).toFloat() });
    }
    return result;
}
