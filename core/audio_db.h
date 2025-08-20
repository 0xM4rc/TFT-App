#ifndef AUDIO_DB_H
#define AUDIO_DB_H

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QList>
#include <QtTypes>

/**
 * @brief Registro de pico (min/max) con metadatos
 */
struct PeakRecord {
    qint64 timestamp;
    qint64 blockIndex;
    qint64 sampleOffset;
    float   minValue;
    float   maxValue;
};

/**
 * @brief Clase para manejar almacenamiento de audio en SQLite
 */
class AudioDb : public QObject
{
    Q_OBJECT

public:
    explicit AudioDb(const QString& dbPath, QObject* parent = nullptr);
    ~AudioDb();

    /** Inicializa la base de datos y crea las tablas necesarias */
    bool initialize();

    void shutdown();

    /** Borra todos los datos y reinicia contadores */
    bool clearDatabase();

    /** Inserta un bloque de audio (raw) */
    bool insertBlock(qint64 blockIndex,
                     qint64 sampleOffset,
                     const QByteArray& audioData,
                     quint64 timestampNs);

    /** Inserta un registro de pico (min/max) */
    bool insertPeak(qint64 blockIndex,
                    qint64 sampleOffset,
                    float minValue,
                    float maxValue,
                    quint64 timestampNs);

    /** Obtiene todos los bloques de audio en orden */
    QList<QByteArray> getAllAudioBlocks() const;

    /** Obtiene un bloque específico */
    QByteArray getAudioBlock(qint64 blockIndex) const;

    /** Obtiene todos los picos en orden */
    QList<QPair<float, float>> getAllPeaks() const;

    /** Obtiene estadísticas generales de la base de datos */
    QString getStatistics() const;

    /** Número total de bloques almacenados */
    int getTotalBlocks() const;

    /** Tamaño total en bytes de todos los bloques */
    qint64 getTotalAudioSize() const;

    /** Devuelve los picos entre dos timestamps */
    QList<PeakRecord> getPeaksByTime(qint64 tStart, qint64 tEnd) const;

    /** Obtiene el blob crudo de un bloque */
    QByteArray getRawBlock(qint64 blockIndex) const;

    /** Obtiene n bloques a partir de un sampleOffset */
    QList<QByteArray> getBlocksByOffset(qint64 offsetStart, int nBlocks) const;

    /** Devuelve el timestamp (ns) de un bloque dado */
    quint64 getBlockTimestamp(qint64 blockIndex) const;

    /** Devuelve el sampleOffset de un bloque dado */
    qint64  getBlockSampleOffset(qint64 blockIndex) const;

signals:
    /** Emitido al ocurrir un error en la base de datos */
    void errorOccurred(const QString& error) const;

private:
    bool createTables();
    bool executeQuery(const QString& query, const QString& operation = "");
    void logError(const QString& operation, const QSqlError& error) const;

    QString      m_dbPath;
    QSqlDatabase m_db;
    bool         m_initialized = false;
};

#endif // AUDIO_DB_H
