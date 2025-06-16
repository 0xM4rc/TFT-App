#include <QApplication>
#include <QDebug>
#include <QUrl>
#include <QAudioSink>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QTimer>

#include "include/gui/rt_mainwindow.h"
#include "include/network_source.h"

int main(int argc, char *argv[])
{
    // 1) QApplication para habilitar la GUI
    QApplication app(argc, argv);

    qDebug() << "=== TEST NETWORK SOURCE CON AUTO-DETECCIÓN Y GUI ===";

    // 2) Instanciar y mostrar RTMainWindow
    RTMainWindow window;
    window.setWindowTitle(QObject::tr("Real-Time Audio Visualizer"));
    window.resize(1024, 600);
    window.show();

    // 3) Lógica original de NetworkSource + QAudioSink
    NetworkSource src;

    QAudioSink*  audioSink       = nullptr;
    QIODevice*   audioDevice     = nullptr;
    bool         audioInitialized = false;

    auto cleanupAudio = [&]() {
        if (audioSink) {
            audioSink->stop();
            delete audioSink;
            audioSink       = nullptr;
            audioDevice     = nullptr;
            audioInitialized = false;
            qDebug() << "Audio sink limpiado";
        }
    };

    auto initializeAudio = [&](const QAudioFormat &detectedFormat) {
        if (audioInitialized) return;

        qDebug() << "Inicializando audio con formato detectado:";
        qDebug() << "  Sample Rate:" << detectedFormat.sampleRate() << "Hz";
        qDebug() << "  Canales:"    << detectedFormat.channelCount();
        qDebug() << "  Formato:"    << detectedFormat.sampleFormat();

        QAudioDevice defaultDev = QMediaDevices::defaultAudioOutput();
        QAudioFormat audioFormat = detectedFormat;

        if (!defaultDev.isFormatSupported(audioFormat)) {
            qWarning() << "Formato detectado no soportado, usando preferido";
            audioFormat = defaultDev.preferredFormat();
        }

        audioSink   = new QAudioSink(defaultDev, audioFormat, &app);
        audioSink->setBufferSize(16384);
        audioDevice = audioSink->start();
        if (!audioDevice) {
            qCritical() << "ERROR: No se pudo iniciar el dispositivo de audio";
            delete audioSink;
            audioSink = nullptr;
            return;
        }

        audioInitialized = true;
        qDebug() << "✓ Audio sink inicializado correctamente;"
                 << "Buffer size:" << audioSink->bufferSize();
    };

    // Conexiones idénticas a tu main original
    QObject::connect(&src, &NetworkSource::stateChanged, [&](bool active) {
        qDebug() << ">>> Stream estado:" << (active ? "ACTIVO" : "PARADO");
        if (!active) cleanupAudio();
    });

    QObject::connect(&src, &NetworkSource::error, [&](const QString &err) {
        qCritical() << ">>> ERROR de stream:" << err;
        cleanupAudio();
    });

    QObject::connect(&src, &NetworkSource::formatDetected, [&](const QAudioFormat &fmt) {
        qDebug() << ">>> FORMATO DETECTADO:";
        initializeAudio(fmt);
    });

    QObject::connect(&src, &NetworkSource::dataReady, [&]() {
        QByteArray chunk = src.getData();
        if (chunk.isEmpty()) return;

        if (!audioInitialized) {
            qDebug() << "Usando formato fallback";
            initializeAudio(src.format());
        }

        if (audioDevice && audioSink && audioSink->state() != QAudio::StoppedState) {
            qint64 written = audioDevice->write(chunk);
            if (written < 0) {
                qWarning() << "Error al escribir audio:" << written;
            }
        }
    });

    // URL de streaming (argumento o por defecto)
    QUrl streamUrl = (argc > 1)
                         ? QUrl::fromUserInput(argv[1])
                         : QUrl("http://stream.radioparadise.com/rock-flac");
    src.setUrl(streamUrl);
    src.start();

    // Estadísticas periódicas
    QTimer statsTimer(&app);
    QObject::connect(&statsTimer, &QTimer::timeout, [&]() {
        if (src.isActive()) {
            qDebug() << "--- ESTADÍSTICAS ---";
            qDebug() << "Stream activo:" << src.isActive()
                     << "Fuente:" << src.sourceName();
            if (audioSink) {
                qDebug() << "Audio estado:"    << audioSink->state()
                << "Buffer libre:"     << audioSink->bytesFree()
                << "Procesados(ms):"   << audioSink->processedUSecs()/1000;
            }
        }
    });
    statsTimer.start(10000);

    // Cleanup al cerrar la app
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&]() {
        qDebug() << "Deteniendo stream...";
        src.stop();
        cleanupAudio();
        qDebug() << "Aplicación terminada";
    });

    // 4) Arrancar el bucle de eventos de Qt (GUI + streaming)
    return app.exec();
}
