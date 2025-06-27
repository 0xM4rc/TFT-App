#ifndef DISK_BUFFER_H
#define DISK_BUFFER_H

#include <QObject>
#include <QFile>
#include <QThread>
#include <QMutex>
#include <QVector>

class DiskBuffer : public QObject {
    Q_OBJECT

public:
    /// @param segmentSizeBytes  Tamaño en bytes tras el cual vuelca a disco
    explicit DiskBuffer(qint64 segmentSizeBytes, const QString& filePath, QObject* parent = nullptr);
    ~DiskBuffer();

public slots:
    /// Slot que conecta a chunkReady(buffer, timestamp)
    void writeChunk(const QByteArray& pcm, qint64 timestamp);

    /// Llamar para cerrar y finalizar
    void stop();

signals:
    void error(const QString& what);

private:
    void run();            // Función para el hilo de escritura
    void flushBuffer();    // Vuelca m_bufferBytes a disco

    qint64       m_segmentSize;
    QString      m_filePath;
    QFile        m_file;
    QThread      m_thread;

    QMutex       m_mutex;
    QByteArray   m_bufferBytes;
    QVector<qint64> m_bufferTimestamps; // para índice opcional
    bool         m_running = true;
};

#endif // DISK_BUFFER_H
