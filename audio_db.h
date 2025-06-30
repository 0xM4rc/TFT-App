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
 * @brief Clase para manejar el almacenamiento de audio en base de datos SQLite
 */
class AudioDb : public QObject
{
    Q_OBJECT

public:
    explicit AudioDb(const QString& dbPath, QObject* parent = nullptr);
    ~AudioDb();

    /**
     * @brief Inicializa la base de datos y crea las tablas necesarias
     * @return true si se inicializó correctamente
     */
    bool initialize();

    /**
     * @brief Limpia toda la base de datos
     * @return true si se limpió correctamente
     */
    bool clearDatabase();

    /**
     * @brief Inserta un bloque de audio
     * @param blockIndex Índice del bloque
     * @param sampleOffset Offset de muestras
     * @param audioData Datos de audio en formato float32
     * @return true si se insertó correctamente
     */
    bool insertBlock(qint64 blockIndex, qint64 sampleOffset, const QByteArray& audioData);

    /**
     * @brief Inserta información de picos (min/max)
     * @param blockIndex Índice del bloque
     * @param sampleOffset Offset de muestras
     * @param minValue Valor mínimo
     * @param maxValue Valor máximo
     * @return true si se insertó correctamente
     */
    bool insertPeak(qint64 blockIndex, qint64 sampleOffset, float minValue, float maxValue);

    /**
     * @brief Obtiene todos los bloques de audio ordenados
     * @return Lista de bloques de audio
     */
    QList<QByteArray> getAllAudioBlocks() const;

    /**
     * @brief Obtiene un bloque específico de audio
     * @param blockIndex Índice del bloque
     * @return Datos del bloque o array vacío si no existe
     */
    QByteArray getAudioBlock(qint64 blockIndex) const;

    /**
     * @brief Obtiene todos los picos ordenados
     * @return Lista de pares (min, max)
     */
    QList<QPair<float, float>> getAllPeaks() const;

    /**
     * @brief Obtiene estadísticas de la base de datos
     * @return String con información estadística
     */
    QString getStatistics() const;

    /**
     * @brief Obtiene el número total de bloques almacenados
     * @return Número de bloques
     */
    int getTotalBlocks() const;

    /**
     * @brief Obtiene el tamaño total de datos de audio
     * @return Tamaño en bytes
     */
    qint64 getTotalAudioSize() const;

signals:
    /**
     * @brief Emitido cuando ocurre un error en la base de datos
     * @param error Descripción del error
     */
    void errorOccurred(const QString& error);

private:
    bool createTables();
    bool executeQuery(const QString& query, const QString& operation = "");
    void logError(const QString& operation, const QSqlError& error);

private:
    QString m_dbPath;
    QSqlDatabase m_db;
    bool m_initialized = false;
};

#endif // AUDIO_DB_H
