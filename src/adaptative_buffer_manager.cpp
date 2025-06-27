#include "include/adaptative_buffer_manager.h"
#include <QMetaObject>
#include <QDebug>

AdaptiveBufferManager::AdaptiveBufferManager(SourceController* srcCtrl,
                                             QThread* procThread,
                                             int desiredLatencyMs,
                                             int initialMultiplier,
                                             int adjustIntervalMs,
                                             QObject* parent)
    : QObject(parent)
    , m_srcCtrl(srcCtrl)
    , m_procThread(procThread)
    , m_desiredLatencyMs(desiredLatencyMs)
    , m_multiplier(initialMultiplier)
    , m_overrunThreshold(0.01f)
{
    // Timer de ajuste
    m_adjustTimer.setInterval(adjustIntervalMs);
    connect(&m_adjustTimer, &QTimer::timeout,
            this, &AdaptiveBufferManager::onAdjustTimeout);
    // Conectar captura de datos para contar bloques
    connect(m_srcCtrl, &SourceController::dataReady,
            this, &AdaptiveBufferManager::onDataReady,
            Qt::QueuedConnection);
}

AdaptiveBufferManager::~AdaptiveBufferManager()
{
    stop();
}

void AdaptiveBufferManager::start()
{
    setupBuffer();
    m_srcCtrl->start();
    m_adjustTimer.start();
}

void AdaptiveBufferManager::stop()
{
    m_adjustTimer.stop();
    if (m_srcCtrl) m_srcCtrl->stop();
    if (m_procThread && m_audioBuffer) {
        m_procThread->quit();
        m_procThread->wait();
    }
    delete m_audioBuffer;
    m_audioBuffer = nullptr;
}

void AdaptiveBufferManager::setupBuffer()
{
    // Eliminar antiguo
    delete m_audioBuffer;
    m_audioBuffer = nullptr;

    // Calcular blockSize
    QAudioFormat fmt = m_srcCtrl->activeFormat();
    int sr = fmt.sampleRate();
    int blockSize = calcBlockSize(sr);

    // Crear nuevo AudioBuffer
    m_audioBuffer = new AudioBuffer(blockSize, m_multiplier);
    m_audioBuffer->moveToThread(m_procThread);
    if (!m_procThread->isRunning()) m_procThread->start();

    // Conectar buffer
    connect(m_audioBuffer, &AudioBuffer::blockReady,
            this, [this](const QVector<float>&){ m_processedBlocks++; },
            Qt::QueuedConnection);
    connect(m_audioBuffer, &AudioBuffer::bufferOverrun,
            this, [](){ qWarning() << "AdaptiveBufferManager: overrun"; },
            Qt::QueuedConnection);

    emit bufferResized(blockSize, m_multiplier);
    m_processedBlocks = 0;
    m_audioBuffer->resetStats();
}

void AdaptiveBufferManager::onDataReady(SourceType,
                                        const QString&,
                                        const QByteArray& data)
{
    // Increment contador en procesBuffer, pero reenviar al buffer
    if (m_audioBuffer) {
        QMetaObject::invokeMethod(m_audioBuffer,
                                  "enqueueData",
                                  Qt::QueuedConnection,
                                  Q_ARG(QByteArray, data));
    }
}

void AdaptiveBufferManager::onAdjustTimeout()
{
    if (!m_audioBuffer) return;
    auto stats = m_audioBuffer->getStats();
    int dropped = stats.droppedBlocks;
    float ratio = m_processedBlocks
                      ? float(dropped) / float(m_processedBlocks)
                      : 0.0f;
    bool resized = false;

    if (ratio > m_overrunThreshold) {
        m_multiplier = qMin(m_multiplier + 1, 10);
        qDebug() << "Overrun ratio" << ratio
                 << ", increasing multiplier to" << m_multiplier;
        resized = true;
    } else if (dropped == 0 && m_multiplier > 2) {
        m_multiplier--;
        qDebug() << "No overruns, decreasing multiplier to" << m_multiplier;
        resized = true;
    }

    if (resized) setupBuffer();
}

int AdaptiveBufferManager::calcBlockSize(int sampleRate) const
{
    int target = (sampleRate * m_desiredLatencyMs) / 1000;
    return nextPowerOfTwo(target);
}

int AdaptiveBufferManager::nextPowerOfTwo(int v)
{
    int p = 1; while (p < v) p <<= 1; return p;
}
