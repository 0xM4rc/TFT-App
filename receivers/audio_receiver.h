#ifndef AUDIO_RECEIVER_H
#define AUDIO_RECEIVER_H

#include "ireceiver.h"
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QDateTime>

// Reutilizamos ReceiverConfig para configuración unificada
struct ReceiverConfig;

class AudioReceiver : public IReceiver {
    Q_OBJECT
public:
    explicit AudioReceiver(QObject* parent = nullptr);
    ~AudioReceiver() override;

    // Configuración unificada
    void setConfig(const ReceiverConfig& cfg) override;

public slots:
    void start() override;
    void stop() override;

signals:
    // IReceiver ya define chunkReady, errorOccurred, finished, audioFormatDetected

private slots:
    void handleReadyRead();

private:
    // Métodos auxiliares
    QAudioDevice selectAudioDevice() const;
    QAudioFormat setupAudioFormat(const QAudioDevice& device) const;
    bool validateConfiguration(const QAudioDevice& device, const QAudioFormat& format) const;

    // Configuración interna adaptada de ReceiverConfig
    struct AudioConfig {
        int sampleRate = 44100;
        int channelCount = 1;
        QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Float;
        QString deviceName;
        bool usePreferredFormat = false;
        int bufferSize = 4096;
        bool fallbackToPreferred = true;
    } m_cfg;

    // Estado runtime
    QAudioSource* m_audioSource = nullptr;
    QIODevice*    m_ioDevice    = nullptr;
    QAudioFormat  m_currentFormat;
    QAudioDevice  m_currentDevice;
    QVector<float> m_floatBuffer;
};

#endif // AUDIO_RECEIVER_H
