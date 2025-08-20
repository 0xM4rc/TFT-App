#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "core/dsp_worker.h"
#include "qaudioformat.h"
#include "receivers/ireceiver.h"
#include <QObject>

class AudioDb;
class Controller : public QObject
{
    Q_OBJECT
    // ───── PROP. audioSource ya vista antes ─────
    Q_PROPERTY(AudioSource audioSource
                   READ  audioSource
                       WRITE setAudioSource
                           NOTIFY audioSourceChanged)

    // Estado de captura (por si la UI quiere un indicador)
    Q_PROPERTY(bool capturing READ isCapturing NOTIFY capturingChanged)

public:
    enum AudioSource { PhysicalAudioInput, NetworkAudioInput };
    Q_ENUM(AudioSource)

    explicit Controller(QObject *parent = nullptr);
    ~Controller() override;

    // getters
    AudioSource audioSource() const { return m_source; }
    bool        isCapturing() const { return m_capturing; }
    void setRotateDbPerSession(bool on);
    bool rotateDbPerSession() const { return m_rotateDbPerSession; }

public slots:
    void setAudioSource(AudioSource src);

    // aplicación de ajustes
    void setPhysicalConfig(const PhysicalInputConfig &cfg);
    void setNetworkConfig (const NetworkInputConfig  &cfg);

    // control de captura
    void startCapture();
    void stopCapture();

signals:
    void audioSourceChanged(AudioSource);
    void capturingChanged(bool);

    // Re-emisión de eventos del receiver
    void floatChunkReady(QVector<float> chunk, quint64 ts);
    void audioFormatDetected(QAudioFormat fmt);
    void errorOccurred(QString msg);
    void finished();

    // Re-emisión eventod del dsp-worker
    void framesReady(const QVector<FrameData>& batch);
    void statsUpdated(qint64 blocksProcessed,
                      qint64 samplesProcessed,
                      int bufferSize);

    void databaseChanged(const QString& path);

private:
    AudioDb*   m_db        = nullptr;

    bool createReceiver();
    void cleanupReceiver();
    void applyConfigToCurrentReceiver();
    void setupDatabase();
    QString makeRandomDbPath();

    AudioSource   m_source { PhysicalAudioInput };
    IReceiver*    m_receiver { nullptr };
    PhysicalInputConfig   m_physCfg;
    NetworkInputConfig    m_netCfg;
    bool          m_capturing { false };

    // Hilos
    QThread*   m_captureThread = nullptr;
    QThread*     m_dspThread    = nullptr;

    // Control del DSPWorker
    bool createDspWorker();
    void cleanupDspWorker();

    DSPWorker*   m_dspWorker    = nullptr;
    DSPConfig    m_dspConfig;

    bool    m_rotateDbPerSession = true;
    QString m_currentDbPath;
};

#endif // CONTROLLER_H
