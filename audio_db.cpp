// audio_db.cpp
#include "audio_db.h"
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QSqlRecord>
#include <QVariant>

AudioDb::AudioDb(const QString& dbPath, QObject* parent)
    : QObject(parent)
    , m_dbPath(dbPath)
{
    // Asegurar que el directorio existe
    QFileInfo fileInfo(m_dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}

AudioDb::~AudioDb() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

bool AudioDb::initialize() {
    if (m_initialized) {
        return true;
    }

    // Configurar conexión SQLite
    m_db = QSqlDatabase::addDatabase("QSQLITE", "AudioCapture");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        QString error = QString("No se pudo abrir la base de datos: %1")
        .arg(m_db.lastError().text());
        qCritical() << error;
        emit errorOccurred(error);
        return false;
    }

    // Configurar SQLite para mejor rendimiento
    QSqlQuery pragmaQuery(m_db);
    pragmaQuery.exec("PRAGMA synchronous = OFF");
    pragmaQuery.exec("PRAGMA journal_mode = MEMORY");
    pragmaQuery.exec("PRAGMA temp_store = MEMORY");
    pragmaQuery.exec("PRAGMA cache_size = 10000");

    if (!createTables()) {
        return false;
    }

    m_initialized = true;
    qDebug() << "AudioDb inicializada:" << m_dbPath;
    return true;
}

bool AudioDb::clearDatabase() {
    if (!m_initialized) {
        return false;
    }

    QSqlQuery query(m_db);

    // Limpiar tablas
    if (!query.exec("DELETE FROM audio_blocks")) {
        logError("limpiar audio_blocks", query.lastError());
        return false;
    }

    if (!query.exec("DELETE FROM audio_peaks")) {
        logError("limpiar audio_peaks", query.lastError());
        return false;
    }

    // Resetear contadores de autoincremento
    query.exec("DELETE FROM sqlite_sequence WHERE name='audio_blocks'");
    query.exec("DELETE FROM sqlite_sequence WHERE name='audio_peaks'");

    // Vacuum para optimizar
    query.exec("VACUUM");

    qDebug() << "Base de datos limpiada";
    return true;
}

bool AudioDb::insertBlock(qint64 blockIndex, qint64 sampleOffset, const QByteArray& audioData) {
    if (!m_initialized || audioData.isEmpty()) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO audio_blocks (block_index, sample_offset, audio_data, data_size, timestamp) "
                  "VALUES (?, ?, ?, ?, ?)");

    query.addBindValue(blockIndex);
    query.addBindValue(sampleOffset);
    query.addBindValue(audioData);
    query.addBindValue(audioData.size());
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        logError("insertar bloque de audio", query.lastError());
        return false;
    }

    return true;
}

bool AudioDb::insertPeak(qint64 blockIndex, qint64 sampleOffset, float minValue, float maxValue) {
    if (!m_initialized) {
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO audio_peaks (block_index, sample_offset, min_value, max_value, timestamp) "
                  "VALUES (?, ?, ?, ?, ?)");

    query.addBindValue(blockIndex);
    query.addBindValue(sampleOffset);
    query.addBindValue(minValue);
    query.addBindValue(maxValue);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        logError("insertar pico de audio", query.lastError());
        return false;
    }

    return true;
}

QList<QByteArray> AudioDb::getAllAudioBlocks() const {
    QList<QByteArray> blocks;

    if (!m_initialized) {
        return blocks;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT audio_data FROM audio_blocks ORDER BY block_index ASC");

    if (!query.exec()) {
        qWarning() << "Error obteniendo bloques de audio:" << query.lastError().text();
        return blocks;
    }

    while (query.next()) {
        QByteArray audioData = query.value(0).toByteArray();
        blocks.append(audioData);
    }

    qDebug() << "Cargados" << blocks.size() << "bloques de audio desde la BD";
    return blocks;
}

QByteArray AudioDb::getAudioBlock(qint64 blockIndex) const {
    if (!m_initialized) {
        return QByteArray();
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT audio_data FROM audio_blocks WHERE block_index = ?");
    query.addBindValue(blockIndex);

    if (!query.exec()) {
        qWarning() << "Error obteniendo bloque" << blockIndex << ":" << query.lastError().text();
        return QByteArray();
    }

    if (query.next()) {
        return query.value(0).toByteArray();
    }

    return QByteArray();
}

QList<QPair<float, float>> AudioDb::getAllPeaks() const {
    QList<QPair<float, float>> peaks;

    if (!m_initialized) {
        return peaks;
    }

    QSqlQuery query(m_db);
    query.prepare("SELECT min_value, max_value FROM audio_peaks ORDER BY block_index ASC");

    if (!query.exec()) {
        qWarning() << "Error obteniendo picos:" << query.lastError().text();
        return peaks;
    }

    while (query.next()) {
        float minVal = query.value(0).toFloat();
        float maxVal = query.value(1).toFloat();
        peaks.append(qMakePair(minVal, maxVal));
    }

    return peaks;
}

QString AudioDb::getStatistics() const {
    if (!m_initialized) {
        return "Base de datos no inicializada";
    }

    int totalBlocks = getTotalBlocks();
    qint64 totalSize = getTotalAudioSize();

    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM audio_peaks");
    int totalPeaks = 0;
    if (query.next()) {
        totalPeaks = query.value(0).toInt();
    }

    float sizeMB = totalSize / (1024.0f * 1024.0f);
    float durationSeconds = 0.0f;

    if (totalBlocks > 0) {
        // Asumir bloques de 4096 muestras, 44100 Hz, estéreo
        qint64 totalSamples = totalBlocks * 4096;
        durationSeconds = totalSamples / (44100.0f * 2.0f);
    }

    return QString("Estadísticas AudioDb:\n"
                   "- Bloques de audio: %1\n"
                   "- Picos almacenados: %2\n"
                   "- Tamaño total: %.2f MB\n"
                   "- Duración estimada: %.1f segundos")
        .arg(totalBlocks)
        .arg(totalPeaks)
        .arg(sizeMB)
        .arg(durationSeconds);
}

int AudioDb::getTotalBlocks() const {
    if (!m_initialized) {
        return 0;
    }

    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM audio_blocks");

    if (query.next()) {
        return query.value(0).toInt();
    }

    return 0;
}

qint64 AudioDb::getTotalAudioSize() const {
    if (!m_initialized) {
        return 0;
    }

    QSqlQuery query(m_db);
    query.exec("SELECT SUM(data_size) FROM audio_blocks");

    if (query.next()) {
        return query.value(0).toLongLong();
    }

    return 0;
}

bool AudioDb::createTables() {
    // Tabla para bloques de audio
    QString createBlocksTable = R"(
        CREATE TABLE IF NOT EXISTS audio_blocks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            block_index INTEGER NOT NULL,
            sample_offset INTEGER NOT NULL,
            audio_data BLOB NOT NULL,
            data_size INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            UNIQUE(block_index)
        )
    )";

    // Tabla para picos de audio
    QString createPeaksTable = R"(
        CREATE TABLE IF NOT EXISTS audio_peaks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            block_index INTEGER NOT NULL,
            sample_offset INTEGER NOT NULL,
            min_value REAL NOT NULL,
            max_value REAL NOT NULL,
            timestamp INTEGER NOT NULL,
            UNIQUE(block_index)
        )
    )";

    // Crear índices para mejor rendimiento
    QString createBlocksIndex = "CREATE INDEX IF NOT EXISTS idx_blocks_index ON audio_blocks(block_index)";
    QString createPeaksIndex = "CREATE INDEX IF NOT EXISTS idx_peaks_index ON audio_peaks(block_index)";

    if (!executeQuery(createBlocksTable, "crear tabla audio_blocks")) {
        return false;
    }

    if (!executeQuery(createPeaksTable, "crear tabla audio_peaks")) {
        return false;
    }

    if (!executeQuery(createBlocksIndex, "crear índice bloques")) {
        return false;
    }

    if (!executeQuery(createPeaksIndex, "crear índice picos")) {
        return false;
    }

    qDebug() << "Tablas de base de datos creadas correctamente";
    return true;
}

bool AudioDb::executeQuery(const QString& queryStr, const QString& operation) {
    QSqlQuery query(m_db);

    if (!query.exec(queryStr)) {
        logError(operation, query.lastError());
        return false;
    }

    return true;
}

void AudioDb::logError(const QString& operation, const QSqlError& error) {
    QString errorMsg = QString("Error en %1: %2").arg(operation).arg(error.text());
    qCritical() << errorMsg;
    emit errorOccurred(errorMsg);
}

QList<PeakRecord> AudioDb::getPeaksByTime(qint64 tStart, qint64 tEnd) const
{
    QList<PeakRecord> out;
    if (!m_initialized) return out;

    QSqlQuery q(m_db);
    q.prepare(R"(
        SELECT timestamp, min_value, max_value
          FROM audio_peaks
         WHERE timestamp BETWEEN ? AND ?
         ORDER BY timestamp ASC
    )");
    q.addBindValue(tStart);
    q.addBindValue(tEnd);

    if (!q.exec()) {
        qWarning() << "Error leyendo picos por tiempo:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        PeakRecord rec;
        rec.timestamp = q.value(0).toLongLong();
        rec.minValue  = q.value(1).toFloat();
        rec.maxValue  = q.value(2).toFloat();
        out.append(rec);
    }
    return out;
}

QByteArray AudioDb::getRawBlock(qint64 blockIndex) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT audio_data FROM audio_blocks WHERE block_index = ?");
    q.addBindValue(blockIndex);
    if (!q.exec() || !q.next()) return {};
    return q.value(0).toByteArray();
}
