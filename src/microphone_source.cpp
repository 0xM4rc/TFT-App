#include "include/microphone_source.h"
#include <QDebug>
#include <QAudioSource>

MicrophoneSource::MicrophoneSource(QObject *parent)
    : AudioSource(parent)
    , m_audioSource(nullptr)
    , m_inputDevice(nullptr)
    , m_device(QMediaDevices::defaultAudioInput())
    , m_readTimer(std::make_unique<QTimer>(this))
{
    initializeFormat();
    // Timer para leer datos periódicamente
    m_readTimer->setInterval(10); // 10ms
    connect(m_readTimer.get(), &QTimer::timeout, this, &MicrophoneSource::handleAudioData);
}

MicrophoneSource::~MicrophoneSource()
{
    stop();
}

void MicrophoneSource::initializeFormat()
{
    // Configurar formato común
    m_format = m_device.preferredFormat();
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

void MicrophoneSource::start()
{
    if (m_active) return;

    try {
        // En Qt6, usamos QAudioSource en lugar de QAudioInput para captura de bajo nivel
        m_audioSource = std::make_unique<QAudioSource>(m_device, m_format, this);

        // Conectar señales de error y estado
        connect(m_audioSource.get(), &QAudioSource::stateChanged,
                this, &MicrophoneSource::handleStateChanged);

        // Iniciar la captura de audio
        m_inputDevice = m_audioSource->start();

        if (m_inputDevice) {
            m_readTimer->start();
            m_active = true;
            emit stateChanged(sourceType(), sourceId(), m_active);
            qDebug() << "Microphone started successfully";
        } else {
            emit error(sourceType(), sourceId(),"Failed to start audio input device");
        }
    } catch (const std::exception &e) {
        emit error(sourceType(), sourceId(), QString("Exception starting microphone: %1").arg(e.what()));
    }
}

void MicrophoneSource::stop()
{
    if (!m_active) return;

    m_readTimer->stop();

    if (m_audioSource) {
        m_audioSource->stop();
        m_audioSource.reset(); // En lugar de delete
    }

    m_inputDevice = nullptr;
    m_active = false;
    emit stateChanged(sourceType(), sourceId(), m_active);
    qDebug() << "Microphone stopped";
}

bool MicrophoneSource::isActive() const
{
    return m_active && m_audioSource && m_audioSource->state() == QAudio::ActiveState;
}

QByteArray MicrophoneSource::getData()
{
    QByteArray data = std::move(m_buffer);
    m_buffer.clear();
    return data;
}

QAudioFormat MicrophoneSource::format() const
{
    return m_format;
}

QString MicrophoneSource::sourceName() const
{
    return "Microphone";
}

void MicrophoneSource::handleAudioData()
{
    if (!m_inputDevice || !m_active) return;

    QByteArray data = m_inputDevice->readAll();
    if (!data.isEmpty()) {
        m_buffer.append(data);
        emit dataReady(sourceType(), sourceId());
    }
}

void MicrophoneSource::handleStateChanged(QAudio::State state)
{
    qDebug() << "Audio state changed:" << state;

    switch (state) {
    case QAudio::ActiveState:
        if (!m_active) {
            m_active = true;
            emit stateChanged(sourceType(), sourceId(), m_active);
        }
        break;
    case QAudio::StoppedState:
        if (m_active) {
            m_active = false;
            emit stateChanged(sourceType(), sourceId(), m_active);
        }
        break;
    case QAudio::IdleState:
        // Audio device is idle but still active
        break;
    case QAudio::SuspendedState:
        // Audio is suspended
        break;
    }
}

void MicrophoneSource::handleErrorOccurred()
{
    if (m_audioSource) {
        emit error(sourceType(), sourceId(),QString("Audio source error: %1").arg(m_audioSource->error()));
    }
}

void MicrophoneSource::setDevice(const QAudioDevice &device)
{
    if (m_active) {
        qWarning() << "Cannot change device while active";
        return;
    }

    m_device = device;
    initializeFormat();
}

void MicrophoneSource::setFormat(const QAudioFormat &format)
{
    if (m_active) {
        qWarning() << "Cannot change format while active";
        return;
    }

    m_format = format;
}
