#ifndef AUDIO_RECEIVER_H
#define AUDIO_RECEIVER_H

#include "ireceiver.h"
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QDateTime>

class AudioReceiver : public IReceiver {
    Q_OBJECT
public:
    explicit AudioReceiver(QObject *parent = nullptr);
    ~AudioReceiver() override;

    /* --- configuración --- */
    bool setConfig(const IReceiverConfig& cfg) override;
    QAudioDevice selectAudioDevice() const;
    QAudioFormat setupAudioFormat(const QAudioDevice &device) const;
    bool validateConfiguration(const QAudioDevice& device,
                                const QAudioFormat& format) const;

public slots:
    void start() override;
    void stop()  override;

private slots:
    void handleReadyRead();

private:
    PhysicalInputConfig m_cfg;

    QAudioSource *m_audioSource = nullptr;
    QIODevice    *m_ioDevice    = nullptr;
    QAudioFormat  m_currentFormat;
    QAudioDevice  m_currentDevice;
    QVector<float> m_floatBuffer;

    bool applyConfig(const PhysicalInputConfig& cfg);
};

#endif // AUDIO_RECEIVER_H
