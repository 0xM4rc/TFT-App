#include "include/audio_loader.h"
#include <QDebug>
#include <QFileInfo>
#include <QAudioFormat>
#include <QtMath>

AudioLoader::AudioLoader(QObject *parent)
    : QObject(parent)
    , m_decoder(new QAudioDecoder(this))
    , m_state(Idle)
    , m_totalDuration(0)
{
    // Conectar señales del decoder
    connect(m_decoder, &QAudioDecoder::bufferReady,
            this, &AudioLoader::onBufferReady);
    connect(m_decoder, &QAudioDecoder::finished,
            this, &AudioLoader::onFinished);
    connect(m_decoder, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
            this, &AudioLoader::onDecoderError);
    connect(m_decoder, &QAudioDecoder::positionChanged,
            this, &AudioLoader::onPositionChanged);
    connect(m_decoder, &QAudioDecoder::durationChanged,
            this, &AudioLoader::onDurationChanged);
}

bool AudioLoader::load(const QString &filePath) {
    // Validar archivo
    if (!validateFile(filePath)) {
        return false;
    }

    // Limpiar estado previo
    clear();
    m_currentFile = filePath;

    // Configurar decoder
    m_decoder->stop();
    m_decoder->setSource(QUrl::fromLocalFile(filePath));

    // Configurar formato de salida preferido (float 32-bit)
    QAudioFormat preferredFormat;
    preferredFormat.setSampleRate(44100); // Se ajustará automáticamente
    preferredFormat.setChannelCount(2);   // Se ajustará automáticamente
    preferredFormat.setSampleFormat(QAudioFormat::Float);
    m_decoder->setAudioFormat(preferredFormat);

    setState(Loading);
    m_decoder->start();

    // Verificar si hubo error inmediato
    if (m_decoder->error() != QAudioDecoder::NoError) {
        setError("Failed to start decoder: " + m_decoder->errorString());
        return false;
    }

    return true;
}

void AudioLoader::stop() {
    if (m_decoder && m_state == Loading) {
        m_decoder->stop();
        setState(Idle);
    }
}

void AudioLoader::clear() {
    m_audio.clear();
    m_lastError.clear();
    m_currentFile.clear();
    m_totalDuration = 0;
    if (m_state != Loading) {
        setState(Idle);
    }
}

bool AudioLoader::validateFile(const QString &filePath) {
    if (filePath.isEmpty()) {
        setError("File path is empty");
        return false;
    }

    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        setError("File does not exist: " + filePath);
        return false;
    }

    if (!fileInfo.isReadable()) {
        setError("File is not readable: " + filePath);
        return false;
    }

    if (fileInfo.size() == 0) {
        setError("File is empty: " + filePath);
        return false;
    }

    return true;
}

void AudioLoader::setState(State newState) {
    if (m_state != newState) {
        m_state = newState;
        emit stateChanged(m_state);
    }
}

void AudioLoader::setError(const QString &errorMessage) {
    m_lastError = errorMessage;
    setState(Error);
    emit error(errorMessage);
}

void AudioLoader::onBufferReady() {
    QAudioBuffer buffer = m_decoder->read();
    if (!buffer.isValid()) {
        return;
    }

    processBuffer(buffer);
}

void AudioLoader::processBuffer(const QAudioBuffer &buffer) {
    QAudioFormat format = buffer.format();

    // Inicializar estructura de audio en el primer buffer
    if (m_audio.sampleRate == 0) {
        m_audio.sampleRate = format.sampleRate();
        m_audio.channels.resize(format.channelCount());

        qDebug() << "Audio format:"
                 << "Sample Rate:" << format.sampleRate()
                 << "Channels:" << format.channelCount()
                 << "Sample Format:" << format.sampleFormat();
    }

    // Validar consistencia del formato
    if (m_audio.sampleRate != format.sampleRate()) {
        setError("Inconsistent sample rate in audio file");
        return;
    }

    if (m_audio.channels.size() != format.channelCount()) {
        setError("Inconsistent channel count in audio file");
        return;
    }

    convertSamples(buffer, format);
}

void AudioLoader::convertSamples(const QAudioBuffer &buffer, const QAudioFormat &format) {
    const int channelCount = format.channelCount();
    const int frameCount = buffer.frameCount();

    // Reservar memoria de una vez para mejor rendimiento
    for (int c = 0; c < channelCount; ++c) {
        m_audio.channels[c].reserve(m_audio.channels[c].size() + frameCount);
    }

    // Convertir según el formato de muestra
    switch (format.sampleFormat()) {
    case QAudioFormat::Float: {
        const float *data = buffer.constData<float>();
        for (int i = 0; i < frameCount; ++i) {
            for (int c = 0; c < channelCount; ++c) {
                int index = i * channelCount + c;
                m_audio.channels[c].append(data[index]);
            }
        }
        break;
    }

    case QAudioFormat::Int32: {
        const qint32 *data = buffer.constData<qint32>();
        constexpr float scale = 1.0f / 2147483648.0f; // 2^31
        for (int i = 0; i < frameCount; ++i) {
            for (int c = 0; c < channelCount; ++c) {
                int index = i * channelCount + c;
                float sample = data[index] * scale;
                m_audio.channels[c].append(sample);
            }
        }
        break;
    }

    case QAudioFormat::Int16: {
        const qint16 *data = buffer.constData<qint16>();
        constexpr float scale = 1.0f / 32768.0f; // 2^15
        for (int i = 0; i < frameCount; ++i) {
            for (int c = 0; c < channelCount; ++c) {
                int index = i * channelCount + c;
                float sample = data[index] * scale;
                m_audio.channels[c].append(sample);
            }
        }
        break;
    }

    case QAudioFormat::UInt8: {
        const quint8 *data = buffer.constData<quint8>();
        constexpr float scale = 1.0f / 128.0f;
        constexpr float offset = -1.0f;
        for (int i = 0; i < frameCount; ++i) {
            for (int c = 0; c < channelCount; ++c) {
                int index = i * channelCount + c;
                float sample = (data[index] * scale) + offset;
                m_audio.channels[c].append(sample);
            }
        }
        break;
    }

    default:
        setError("Unsupported audio sample format");
        return;
    }
}

void AudioLoader::onFinished() {
    if (m_audio.isEmpty()) {
        setError("No audio data was loaded");
        return;
    }

    // Verificar que todos los canales tienen el mismo número de muestras
    if (!m_audio.channels.isEmpty()) {
        int expectedFrames = m_audio.channels[0].size();
        for (int c = 1; c < m_audio.channels.size(); ++c) {
            if (m_audio.channels[c].size() != expectedFrames) {
                setError("Channel length mismatch in loaded audio");
                return;
            }
        }
    }

    setState(Loaded);
    emit loaded();

    qDebug() << "Audio loaded successfully:"
             << "Duration:" << m_audio.durationSeconds() << "seconds"
             << "Frames:" << m_audio.frameCount()
             << "Channels:" << m_audio.channelCount()
             << "Sample Rate:" << m_audio.sampleRate;
}

void AudioLoader::onDecoderError() {
    QString errorMsg = "Decoder error";
    if (m_decoder) {
        errorMsg += ": " + m_decoder->errorString();
    }
    setError(errorMsg);
}

void AudioLoader::onPositionChanged(qint64 position) {
    if (m_totalDuration > 0) {
        emit progressChanged(position, m_totalDuration);
    }
}

void AudioLoader::onDurationChanged(qint64 duration) {
    m_totalDuration = duration;
}
