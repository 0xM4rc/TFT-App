#ifndef AUDIO_RECEIVER_H
#define AUDIO_RECEIVER_H

#include "ireceiver.h"
#include <QAudioSource>
#include <QMediaDevices>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QIODevice>
#include <QDateTime>

struct AudioConfig {
    // Configuración del formato de audio
    int sampleRate = 44100;              // Frecuencia de muestreo (Hz)
    int channelCount = 1;                // Número de canales (1=mono, 2=estéreo)
    QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Float;

    // Configuración del dispositivo
    QString deviceName;                  // Nombre específico del dispositivo (vacío = por defecto)
    bool useDefaultDevice = true;        // Usar dispositivo por defecto

    // Configuración de buffer
    int bufferSize = 4096;               // Tamaño del buffer interno

    // Configuración de comportamiento
    bool usePreferredFormat = false;     // Si es true, ignora la config manual y usa el formato preferido del dispositivo
    bool fallbackToPreferred = true;    // Si la config manual falla, intentar con formato preferido

    // Constructor por defecto con valores sensatos
    AudioConfig() = default;

    // Constructor con parámetros básicos
    AudioConfig(int rate, int channels, QAudioFormat::SampleFormat format = QAudioFormat::Int16)
        : sampleRate(rate), channelCount(channels), sampleFormat(format) {}
};

class AudioReceiver : public IReceiver {
    Q_OBJECT

public:
    explicit AudioReceiver(QObject* parent = nullptr);
    explicit AudioReceiver(const AudioConfig& config, QObject* parent = nullptr);
    ~AudioReceiver() override;

    // Configuración
    void setAudioConfig(const AudioConfig& config);
    AudioConfig getAudioConfig() const;

    // Información del estado actual
    QAudioFormat getCurrentFormat() const;
    QAudioDevice getCurrentDevice() const;
    QString getDeviceInfo() const;

public slots:
    void start() override;
    void stop() override;

private slots:
    void handleReadyRead();

private:
    // Métodos auxiliares
    QAudioDevice selectAudioDevice() const;
    QAudioFormat setupAudioFormat(const QAudioDevice& device) const;
    bool validateConfiguration(const QAudioDevice& device, const QAudioFormat& format) const;

    // Miembros
    QAudioSource* m_audioSource = nullptr;
    QIODevice*    m_ioDevice = nullptr;
    AudioConfig   m_config;

    // Estado actual
    QAudioFormat  m_currentFormat;
    QAudioDevice  m_currentDevice;

    QVector<float> m_floatBuffer;
};

#endif // AUDIO_RECEIVER_H
