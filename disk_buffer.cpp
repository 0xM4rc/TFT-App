#include "disk_buffer.h"
#include <QDateTime>
#include <QDebug>

DiskBuffer::DiskBuffer(qint64 segmentSizeBytes, const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_segmentSize(segmentSizeBytes)
    , m_filePath(filePath)
{
    // Mover este objeto al hilo propio
    this->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, this, &DiskBuffer::run);
    m_thread.start();
}

DiskBuffer::~DiskBuffer() {
    stop();
    m_thread.quit();
    m_thread.wait();
}

void DiskBuffer::writeChunk(const QByteArray& pcm, qint64 timestamp) {
    QMutexLocker lock(&m_mutex);
    if (!m_running) return;
    // Acumular datos y timestamp
    m_bufferBytes.append(pcm);
    m_bufferTimestamps.append(timestamp);
    // Si superamos el umbral, notificamos al hilo que flushee
}

void DiskBuffer::stop() {
    {
        QMutexLocker lock(&m_mutex);
        m_running = false;
    }
    // Despertar hilo para que salga
    QMetaObject::invokeMethod(this, "run", Qt::QueuedConnection);
}

void DiskBuffer::run() {
    QMutexLocker lock(&m_mutex);
    if (!m_running && m_bufferBytes.isEmpty()) {
        // Terminamos
        if (m_file.isOpen()) m_file.close();
        return;
    }

    if (m_bufferBytes.size() >= m_segmentSize) {
        lock.unlock();        // liberar mientras escribimos
        flushBuffer();
        lock.relock();
    }
    // Volver a consultarse tras un rato, para no busy-loop
    lock.unlock();
    QThread::msleep(100);
    // Reinvocar run() hasta que stop cierre todo
    QMetaObject::invokeMethod(this, "run", Qt::QueuedConnection);
}

void DiskBuffer::flushBuffer() {
    // Abrir archivo si no lo está
    if (!m_file.isOpen()) {
        QString timestamp = QDateTime::currentDateTime()
        .toString("yyyyMMdd_HHmmss");
        QString path = m_filePath.arg(timestamp);
        m_file.setFileName(path);
        if (!m_file.open(QIODevice::WriteOnly)) {
            emit error(QString("No se pudo abrir %1").arg(path));
            return;
        }
    }

    // Escribir todo lo acumulado
    QByteArray toWrite;
    QVector<qint64> timestamps;
    {
        QMutexLocker lock(&m_mutex);
        toWrite.swap(m_bufferBytes);
        timestamps.swap(m_bufferTimestamps);
    }

    qint64 written = m_file.write(toWrite);
    if (written != toWrite.size()) {
        emit error("Error al escribir datos a disco");
    } else {
        qDebug() << "Vuelto segmento de" << written << "bytes a"
                 << m_file.fileName();
    }
    m_file.flush();

    // Aquí podrías guardar en un SQLite o JSON el índice:
    // (por ejemplo: fichero, timestampInicio=timestamps.first(), length=written)
}
