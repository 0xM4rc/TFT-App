#include "audio_receiver.h"
#include <QDebug>
#include <cstring>

AudioReceiver::AudioReceiver(QObject* parent)
    : IReceiver(parent)
{
    // valores por defecto ya en m_cfg
}

AudioReceiver::~AudioReceiver() {
    stop();
}

bool AudioReceiver::setConfig(const IReceiverConfig& cfg)
{
    // Intentamos convertir a PhysicalInputConfig
    const auto *phys = dynamic_cast<const PhysicalInputConfig*>(&cfg);
    if (!phys) {
        qWarning() << "Config incompatible: se esperaba PhysicalInputConfig";
        emit errorOccurred(QStringLiteral("Config incompatible: se esperaba PhysicalInputConfig"));
        return false;
    }
    return applyConfig(*phys);
}

bool AudioReceiver::applyConfig(const PhysicalInputConfig &cfg)
{
    if (m_audioSource) {
        qWarning() << "No se puede cambiar configuración mientras captura está activa";
        emit errorOccurred(QStringLiteral("No se puede cambiar configuración mientras la captura está activa"));
        return false;
    }

    if (int err = cfg.isValid(); err != 0) {
        qWarning() << "PhysicalInputConfig inválido, código" << err;
        emit errorOccurred(QStringLiteral("PhysicalInputConfig inválido, código %1").arg(err));
        return false;
    }

    m_cfg = cfg;
    return true;
}

void AudioReceiver::start()
{
    if (m_audioSource) {
        qDebug() << "AudioReceiver ya está iniciado";
        return;
    }

    /* ---------- 1. Selección/validación ---------- */
    QAudioDevice dev = selectAudioDevice();
    if (dev.isNull()) {
        qWarning() << "AudioReceiver: no hay dispositivo de entrada";
        emit errorOccurred("No input audio device");
        return;
    }

    QAudioFormat fmt = setupAudioFormat(dev);
    if (!validateConfiguration(dev, fmt)) {
        if (m_cfg.fallbackToPreferred) {
            qWarning() << "Usando formato preferido del dispositivo…";
            fmt = dev.preferredFormat();
            if (!validateConfiguration(dev, fmt)) {
                qWarning() << "Formato preferido tampoco soportado";
                emit errorOccurred("Preferred audio format not supported");
                return;
            }
        } else {
            emit errorOccurred("Audio format not supported");
            return;
        }
    }

    /* ---------- 2. Crear QAudioSource ---------- */
    m_audioSource = new QAudioSource(dev, fmt, this);
    if (m_cfg.bufferSize > 0)
        m_audioSource->setBufferSize(m_cfg.bufferSize);

    m_ioDevice = m_audioSource->start();
    if (!m_ioDevice) {
        qWarning() << "Fallo al iniciar captura";
        emit errorOccurred("Failed to start audio capture");
        delete m_audioSource;
        m_audioSource = nullptr;
        return;
    }

    /* ---------- 3. Estado y buffers ---------- */
    m_currentDevice = dev;
    m_currentFormat = fmt;
    int bps = (fmt.sampleFormat() == QAudioFormat::Int16) ? 2 : 4;
    int maxBytes = m_cfg.bufferSize > 0
                       ? m_cfg.bufferSize
                       : fmt.sampleRate() * fmt.channelCount() * bps;
    m_floatBuffer.reserve(maxBytes / bps);

    /* ---------- 4. readyRead ---------- */
    connect(m_ioDevice, &QIODevice::readyRead,
            this, &AudioReceiver::handleReadyRead);

    qDebug() << "AudioReceiver iniciado:" << m_cfg.deviceId;
    emit audioFormatDetected(fmt);
}


void AudioReceiver::stop() {
    if (!m_audioSource) return;

    qDebug() << "AudioReceiver: deteniendo";
    disconnect(m_ioDevice, &QIODevice::readyRead,
               this, &AudioReceiver::handleReadyRead);
    m_audioSource->stop();
    m_audioSource->deleteLater();
    m_audioSource = nullptr;
    m_ioDevice    = nullptr;
    m_currentDevice = QAudioDevice();
    m_currentFormat = QAudioFormat();
    emit finished();
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
    } else {
        std::memcpy(m_floatBuffer.data(), data, sampleCount * sizeof(float));
    }

    // Convertir timestamp de milisegundos a nanosegundos
    quint64 timestampNs = QDateTime::currentMSecsSinceEpoch() * 1000000ULL;
    // Emitir usando la señal correcta de la interfaz
    emit floatChunkReady(m_floatBuffer, timestampNs);
}

QAudioDevice AudioReceiver::selectAudioDevice() const
{
    if (m_cfg.deviceId.isEmpty())
        return QMediaDevices::defaultAudioInput();

    for (const auto &dev : QMediaDevices::audioInputs()) {
        if (dev.description() == m_cfg.deviceId || dev.id() == m_cfg.deviceId.toUtf8())
            return dev;
    }
    qWarning() << "Dispositivo" << m_cfg.deviceId << "no encontrado, usando por defecto";
    return QMediaDevices::defaultAudioInput();
}

QAudioFormat AudioReceiver::setupAudioFormat(const QAudioDevice &device) const
{
    if (m_cfg.usePreferred)
        return device.preferredFormat();

    QAudioFormat fmt;
    fmt.setSampleRate(m_cfg.sampleRate);
    fmt.setChannelCount(m_cfg.channelCount);
    fmt.setSampleFormat(m_cfg.sampleFormat);
    return fmt;
}

bool AudioReceiver::validateConfiguration(const QAudioDevice& device,
                                          const QAudioFormat& format) const
{
    if (device.isNull()) return false;
    if (!device.isFormatSupported(format)) return false;
    return true;
}
