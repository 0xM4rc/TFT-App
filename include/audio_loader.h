#ifndef AUDIO_LOADER_H
#define AUDIO_LOADER_H

#include <QObject>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QVector>
#include <QString>
#include <QFileInfo>
#include "include/data_structures/file_audio_data.h"
#include <QUrl>

class AudioLoader : public QObject {
    Q_OBJECT

public:
    enum State {
        Idle,       // No está cargando
        Loading,    // Cargando archivo
        Loaded,     // Carga completada exitosamente
        Error       // Error en la carga
    };
    Q_ENUM(State)

    explicit AudioLoader(QObject *parent = nullptr);
    ~AudioLoader() = default;

    // Métodos principales
    bool load(const QString &filePath);
    void stop();
    void clear();

    // Getters
    const FileAudioData& audio() const { return m_audio; }
    State state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QString currentFile() const { return m_currentFile; }

    // Información del archivo actual
    bool isLoaded() const { return m_state == Loaded && !m_audio.isEmpty(); }
    bool isLoading() const { return m_state == Loading; }

signals:
    void loaded();
    void error(const QString &message);
    void stateChanged(State newState);
    void progressChanged(qint64 position, qint64 duration); // Para archivos largos

private slots:
    void onBufferReady();
    void onFinished();
    void onDecoderError();
    void onPositionChanged(qint64 position);
    void onDurationChanged(qint64 duration);

private:
    // Métodos auxiliares
    void setState(State newState);
    void setError(const QString &errorMessage);
    bool validateFile(const QString &filePath);
    void processBuffer(const QAudioBuffer &buffer);
    void convertSamples(const QAudioBuffer &buffer, const QAudioFormat &format);

    // Miembros
    QAudioDecoder *m_decoder;
    FileAudioData m_audio;
    State m_state;
    QString m_lastError;
    QString m_currentFile;
    qint64 m_totalDuration;
};

#endif // AUDIO_LOADER_H
