// AudioReceiver.cpp
#include "audio_receiver.h"
#include <QDebug>
#include <cstring>

AudioReceiver::AudioReceiver(QObject* parent)
    : IReceiver(parent)
{
    // configuración por defecto en m_config
}

AudioReceiver::AudioReceiver(const AudioConfig& config, QObject* parent)
    : IReceiver(parent)
    , m_config(config)
{
}

AudioReceiver::~AudioReceiver() {
    stop();
}

void AudioReceiver::setAudioConfig(const AudioConfig& config) {
    if (m_audioSource) {
        qWarning() << "No se puede cambiar la configuración mientras la captura está activa";
        return;
    }
    m_config = config;
}

AudioConfig AudioReceiver::getAudioConfig() const {
    return m_config;
}

QAudioFormat AudioReceiver::getCurrentFormat() const {
    return m_currentFormat;
}

QAudioDevice AudioReceiver::getCurrentDevice() const {
    return m_currentDevice;
}

QString AudioReceiver::getDeviceInfo() const {
    if (m_currentDevice.isNull()) {
        return QStringLiteral("Sin dispositivo activo");
    }

    QString fmtStr = (m_currentFormat.sampleFormat() == QAudioFormat::Int16) ? QStringLiteral("16-bit")
                     : (m_currentFormat.sampleFormat() == QAudioFormat::Float) ? QStringLiteral("Float")
                                                                               : QStringLiteral("Otro");

    return QStringLiteral("Dispositivo: %1\nFormato: %2 Hz, %3 canales, %4")
        .arg(m_currentDevice.description())
        .arg(m_currentFormat.sampleRate())
        .arg(m_currentFormat.channelCount())
        .arg(fmtStr);
}

QAudioDevice AudioReceiver::selectAudioDevice() const {
    if (m_config.deviceName.isEmpty()) {
        return QMediaDevices::defaultAudioInput();
    }
    for (const auto& dev : QMediaDevices::audioInputs()) {
        if (dev.description() == m_config.deviceName
            || dev.id() == m_config.deviceName.toUtf8()) {
            return dev;
        }
    }
    qWarning() << "Dispositivo" << m_config.deviceName << "no encontrado, usando por defecto";
    return QMediaDevices::defaultAudioInput();
}

QAudioFormat AudioReceiver::setupAudioFormat(const QAudioDevice& device) const {
    if (m_config.usePreferredFormat) {
        return device.preferredFormat();
    }
    QAudioFormat fmt;
    fmt.setSampleRate(m_config.sampleRate);
    fmt.setChannelCount(m_config.channelCount);
    fmt.setSampleFormat(m_config.sampleFormat);
    return fmt;
}

bool AudioReceiver::validateConfiguration(const QAudioDevice& device,
                                          const QAudioFormat& format) const
{
    if (device.isNull()) {
        qWarning() << "AudioReceiver: dispositivo inválido";
        return false;
    }
    if (!device.isFormatSupported(format)) {
        qWarning() << "AudioReceiver: formato no soportado:"
                   << format.sampleRate() << "Hz," << format.channelCount() << "canales";
        return false;
    }
    return true;
}

void AudioReceiver::start() {
    if (m_audioSource) {
        qDebug() << "AudioReceiver ya está iniciado";
        return;
    }

    // 1) Selección y validación
    QAudioDevice dev = selectAudioDevice();
    if (dev.isNull()) {
        qWarning() << "AudioReceiver: no hay dispositivo de entrada";
        return;
    }
    QAudioFormat fmt = setupAudioFormat(dev);
    if (!validateConfiguration(dev, fmt)) {
        if (m_config.fallbackToPreferred) {
            qWarning() << "Usando formato preferido del dispositivo...";
            fmt = dev.preferredFormat();
            if (!validateConfiguration(dev, fmt)) {
                qWarning() << "AudioReceiver: form. preferido tampoco soportado";
                return;
            }
        } else {
            return;
        }
    }

    // 2) Crear fuente y dispositivo Qt
    m_audioSource  = new QAudioSource(dev, fmt, this);
    if (m_config.bufferSize > 0) {
        m_audioSource->setBufferSize(m_config.bufferSize);
    }
    m_ioDevice     = m_audioSource->start();
    if (!m_ioDevice) {
        qWarning() << "AudioReceiver: fallo al iniciar captura";
        delete m_audioSource;
        m_audioSource = nullptr;
        return;
    }

    // 3) Guardar estado y preparar buffer de floats
    m_currentDevice = dev;
    m_currentFormat = fmt;
    int bps = (fmt.sampleFormat() == QAudioFormat::Int16) ? 2 : 4;
    int maxBytes = (m_config.bufferSize > 0
                        ? m_config.bufferSize
                        : fmt.sampleRate() * fmt.channelCount() * bps);
    int maxSamples = maxBytes / bps;
    m_floatBuffer.reserve(maxSamples);

    // 4) Conectar lectura y arrancar
    connect(m_ioDevice, &QIODevice::readyRead,
            this, &AudioReceiver::handleReadyRead);

    qDebug() << "AudioReceiver iniciado:\n" << getDeviceInfo();
}

void AudioReceiver::stop() {
    if (!m_audioSource) return;

    qDebug() << "AudioReceiver: deteniendo";
    if (m_ioDevice) {
        disconnect(m_ioDevice, &QIODevice::readyRead,
                   this, &AudioReceiver::handleReadyRead);
    }
    m_audioSource->stop();
    m_audioSource->deleteLater();
    m_audioSource  = nullptr;
    m_ioDevice     = nullptr;
    m_currentDevice = QAudioDevice();
    m_currentFormat = QAudioFormat();
}

void AudioReceiver::handleReadyRead() {
    if (!m_ioDevice) return;

    QByteArray buffer = m_ioDevice->readAll();
    if (buffer.isEmpty()) return;

    const auto& fmt = m_currentFormat;
    int bytesPerSample = (fmt.sampleFormat() == QAudioFormat::Int16) ? 2 : 4;
    int sampleCount    = buffer.size() / bytesPerSample;
    if (sampleCount <= 0) return;

    m_floatBuffer.resize(sampleCount);
    const char* data = buffer.constData();

    if (fmt.sampleFormat() == QAudioFormat::Int16) {
        const qint16* in = reinterpret_cast<const qint16*>(data);
        for (int i = 0; i < sampleCount; ++i) {
            m_floatBuffer[i] = in[i] / 32768.0f;
        }
    } else if (fmt.sampleFormat() == QAudioFormat::Float) {
        std::memcpy(m_floatBuffer.data(), data, sampleCount * sizeof(float));
    } else {
        // Otros formatos si los necesitas...
        return;
    }

    qint64 ts = QDateTime::currentMSecsSinceEpoch();
    emit chunkReady(m_floatBuffer, ts);
}
