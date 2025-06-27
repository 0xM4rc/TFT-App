#ifndef ADAPTATIVE_BUFFER_MANAGER_H
#define ADAPTATIVE_BUFFER_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QThread>
#include <QAudioFormat>
#include "include/data_structures/audio_buffer.h"
#include "source_controller.h"

/**
 * @brief AdaptiveBufferManager ajusta dinámicamente el AudioBuffer
 *        según estadísticas de overrun y latencia.
 */
class AdaptiveBufferManager : public QObject {
    Q_OBJECT
public:
    AdaptiveBufferManager(SourceController* srcCtrl,
                          QThread* procThread,
                          int desiredLatencyMs = 50,
                          int initialMultiplier = 4,
                          int adjustIntervalMs = 5000,
                          QObject* parent = nullptr);
    ~AdaptiveBufferManager() override;

    void start();
    void stop();

signals:
    void bufferResized(int newBlockSize, int newMultiplier);

private slots:
    void onAdjustTimeout();
    void onDataReady(SourceType, const QString&, const QByteArray& data);

private:
    SourceController* m_srcCtrl;
    QThread*          m_procThread;
    AudioBuffer*      m_audioBuffer{nullptr};
    QTimer            m_adjustTimer;

    int m_desiredLatencyMs;
    int m_multiplier;
    int m_processedBlocks{0};
    float m_overrunThreshold;

    void setupBuffer();
    int  calcBlockSize(int sampleRate) const;
    static int nextPowerOfTwo(int v);
};

#endif // ADAPTATIVE_BUFFER_MANAGER_H
