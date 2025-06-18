#ifndef MICROPHONE_SOURCE_H
#define MICROPHONE_SOURCE_H

#include "interfaces/audio_source.h"
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QTimer>
#include <QAudio>
#include <QMutex>
#include <memory>

class QAudioSource;
class QIODevice;

class MicrophoneSource : public AudioSource
{
    Q_OBJECT

public:
    explicit MicrophoneSource(QObject *parent = nullptr);
    ~MicrophoneSource() override;

    // AudioSource interface
    void start() override;
    void stop() override;
    bool isActive() const override;
    QByteArray getData() override;
    QAudioFormat format() const override;
    QString sourceName() const override;

    // Configuration methods
    void setDevice(const QAudioDevice &device);
    void setFormat(const QAudioFormat &format);
    void setBufferSize(int size);

    // Status methods
    QAudio::Error lastError() const;
    QString lastErrorString() const;
    qint64 bytesReady() const;

    // Volume control
    void setVolume(qreal volume);
    qreal volume() const;

    // Identificadores de interfaz
    SourceType sourceType() const override { return SourceType::Microphone; }
    QString    sourceId()   const override { return m_device.id(); }

private slots:
    void handleAudioData();
    void handleStateChanged(QAudio::State state);
    void handleErrorOccurred();

private:
    void initializeFormat();
    void cleanup();
    bool validateFormat(const QAudioFormat &format) const;
    void resetBuffer();

    // Audio components
    std::unique_ptr<QAudioSource> m_audioSource;
    QIODevice *m_inputDevice;
    QAudioDevice m_device;

    // Buffer management
    QByteArray m_buffer;
    QMutex m_bufferMutex;
    int m_maxBufferSize;

    // Timing
    std::unique_ptr<QTimer> m_readTimer;
    static constexpr int DEFAULT_READ_INTERVAL = 10; // ms

    // Error handling
    QAudio::Error m_lastError;
    QString m_lastErrorString;

    // Volume
    qreal m_volume;
};

#endif // MICROPHONE_SOURCE_H
