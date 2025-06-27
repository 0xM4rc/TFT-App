#include "include/data_structures/audio_buffer.h"
#include <QtEndian>
#include <QDebug>
#include <QElapsedTimer>
#include <algorithm>
#include <cstring>

AudioBuffer::AudioBuffer(int blockSize, int bufferMultiplier, QObject* parent)
    : QObject(parent)
    , m_blockSize(blockSize)
    , m_bufferSize(blockSize * bufferMultiplier)
    , m_buffer(m_bufferSize)
    , m_outputBuffer(blockSize)
{
    // Pre-reservar capacidad para evitar reallocaciones
    m_outputBuffer.reserve(blockSize);

    qDebug() << "AudioBuffer initialized:"
             << "blockSize=" << m_blockSize
             << "bufferSize=" << m_bufferSize;
}

AudioBuffer::~AudioBuffer() = default;

void AudioBuffer::enqueueData(const QByteArray& rawData)
{
    if (rawData.isEmpty()) return;

    QElapsedTimer timer;
    timer.start();

    const int bytesPerSample = 2;
    const int sampleCount = rawData.size() / bytesPerSample;
    const char* data = rawData.constData();

    // Verificar overflow antes de escribir
    const int currentAvailable = m_availableSamples.load(std::memory_order_acquire);
    const int spaceAvailable = m_bufferSize - currentAvailable;

    if (sampleCount > spaceAvailable) {
        // Buffer overflow - descartar datos más antiguos
        m_stats.droppedBlocks++;
        emit bufferOverrun();

        // Opción 1: Descartar nuevos datos
        // return;

        // Opción 2: Sobrescribir datos antiguos (implementado)
    }

    // Conversión y escritura optimizada
    int writePos = m_writePos.load(std::memory_order_relaxed);

    for (int i = 0; i < sampleCount; ++i) {
        // Lectura directa sin memcpy para mejor rendimiento
        qint16 sample16;
        std::memcpy(&sample16, data + i * bytesPerSample, bytesPerSample);
        sample16 = qFromLittleEndian(sample16);

        // Conversión inline optimizada
        m_buffer[writePos] = convertSample(sample16);
        writePos = circularIndex(writePos + 1);
    }

    // Actualizar posiciones atómicamente
    m_writePos.store(writePos, std::memory_order_release);

    // Actualizar contador de muestras disponibles
    const int newAvailable = std::min(currentAvailable + sampleCount, m_bufferSize);
    m_availableSamples.store(newAvailable, std::memory_order_release);

    // Procesar bloques disponibles
    processBuffer();

    // Estadísticas de rendimiento
    updateStats(timer.nsecsElapsed() / 1000);
}

void AudioBuffer::processBuffer()
{
    const int available = m_availableSamples.load(std::memory_order_acquire);

    // Procesar todos los bloques completos disponibles
    int blocksProcessed = 0;
    int remainingAvailable = available;

    while (remainingAvailable >= m_blockSize) {
        const int writePos = m_writePos.load(std::memory_order_acquire);
        const int readPos = circularIndex(writePos - remainingAvailable + m_bufferSize);

        // Copia eficiente del bloque
        if (readPos + m_blockSize <= m_bufferSize) {
            // Copia contigua - más eficiente
            std::memcpy(m_outputBuffer.data(),
                        m_buffer.data() + readPos,
                        m_blockSize * sizeof(float));
        } else {
            // Copia en dos partes (wrap-around)
            const int firstPart = m_bufferSize - readPos;
            const int secondPart = m_blockSize - firstPart;

            std::memcpy(m_outputBuffer.data(),
                        m_buffer.data() + readPos,
                        firstPart * sizeof(float));
            std::memcpy(m_outputBuffer.data() + firstPart,
                        m_buffer.data(),
                        secondPart * sizeof(float));
        }

        // Emitir señal (con mutex mínimo)
        {
            QMutexLocker locker(&m_emitMutex);
            emit blockReady(m_outputBuffer);
        }

        remainingAvailable -= m_blockSize;
        blocksProcessed++;

        // Limitar procesamiento por iteración para evitar bloqueos largos
        if (blocksProcessed >= 4) break;
    }

    // Actualizar contador de muestras disponibles
    if (blocksProcessed > 0) {
        const int newAvailable = available - (blocksProcessed * m_blockSize);
        m_availableSamples.store(newAvailable, std::memory_order_release);
    }
}

AudioBuffer::Stats AudioBuffer::getStats() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

void AudioBuffer::resetStats()
{
    QMutexLocker locker(&m_statsMutex);
    m_stats = Stats{};
}

void AudioBuffer::updateStats(int processingTimeUs) const
{
    QMutexLocker locker(&m_statsMutex);

    m_stats.maxLatencyUs = std::max(m_stats.maxLatencyUs, processingTimeUs);

    // Promedio móvil simple
    const double alpha = 0.1;
    m_stats.avgProcessingTimeUs = alpha * processingTimeUs +
                                  (1.0 - alpha) * m_stats.avgProcessingTimeUs;
}
