#ifndef MICROPHONE_SOURCE_H
#define MICROPHONE_SOURCE_H

#include "interfaces/audio_source.h"
#include <QAudioDevice>
#include <QMediaDevices>
#include <QTimer>
#include <QAudio>

class QAudioSource;
class QIODevice;

class MicrophoneSource : public AudioSource
{
    Q_OBJECT

public:
    explicit MicrophoneSource(QObject *parent = nullptr);
    ~MicrophoneSource();

    void start() override;
    void stop() override;
    bool isActive() const override;
    QByteArray getData() override;
    QAudioFormat format() const override;
    QString sourceName() const override;

    void setDevice(const QAudioDevice &device);
    void setFormat(const QAudioFormat &format);

private slots:
    void handleAudioData();
    void handleStateChanged(QAudio::State state);
    void handleErrorOccurred();

private:
    void initializeFormat();

    QAudioSource *m_audioSource;
    QIODevice *m_inputDevice;
    QAudioDevice m_device;
    QByteArray m_buffer;
    QTimer *m_readTimer;
};

#endif // MICROPHONE_SOURCE_H
