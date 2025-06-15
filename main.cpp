#include <QCoreApplication>
#include <QDebug>
#include <QUrl>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>

#include "include/network_source.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qDebug() << "=== TEST NETWORK SOURCE + AUDIO SINK (sin límite) ===";

    // Instanciar y conectar NetworkSource
    NetworkSource src;
    QObject::connect(&src, &NetworkSource::stateChanged, [&](bool on){
        qDebug() << "Stream estado:" << (on ? "ACTIVO" : "PARADO");
    });
    QObject::connect(&src, &NetworkSource::error, [&](const QString &err){
        qCritical() << "Error:" << err;
    });

    // Audio Sink (se creará al recibir el primer chunk)
    QAudioSink *audioSink = nullptr;
    QIODevice *audioDevice = nullptr;

    QObject::connect(&src, &NetworkSource::dataReady, [&](){
        QByteArray chunk = src.getData();
        if (chunk.isEmpty()) return;

        // Al primer fragmento, inicializamos el QAudioSink
        if (!audioSink) {
            QAudioFormat fmt = src.format();
            QAudioDevice defaultDev = QMediaDevices::defaultAudioOutput();
            if (!defaultDev.isFormatSupported(fmt)) {
                qWarning() << "Formato no soportado, usando preferido";
                fmt = defaultDev.preferredFormat();
            }
            audioSink = new QAudioSink(defaultDev, fmt, &app);
            audioSink->setBufferSize(8192);
            audioDevice = audioSink->start();
            if (!audioDevice) {
                qCritical() << "No pudo iniciarse el dispositivo de audio";
                delete audioSink;
                audioSink = nullptr;
                return;
            }
            qDebug() << "Playback iniciado:"
                     << fmt.sampleRate() << "Hz,"
                     << fmt.channelCount() << "canales,"
                     << fmt.sampleFormat();
        }

        if (audioDevice && audioSink->state() != QAudio::StoppedState) {
            qint64 written = audioDevice->write(chunk);
            if (written < 0) {
                qWarning() << "Error al escribir al audioDevice";
            }
        }
    });

    // Configurar URL y formato de stream
    QUrl url = argc > 1
                   ? QUrl(QString::fromUtf8(argv[1]))
                   : QUrl("http://stream.radioparadise.com/rock-flac");
    qDebug() << "Usando URL:" << url.toString();
    src.setUrl(url);

    QAudioFormat fmt;
    fmt.setSampleRate(44100);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Int16);
    src.setStreamFormat(fmt);

    // Arrancar streaming de forma indefinida
    src.start();
    qDebug() << "Streaming en marcha. Pulsa Ctrl+C para detener.";

    return app.exec();
}
