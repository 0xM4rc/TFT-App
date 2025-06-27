#include "mainwindow.h"
#include <QUrl>
#include <QDebug>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , player(new QMediaPlayer(this))
    , audioOutput(new QAudioOutput(this))
    , micReceiver(nullptr)
    , audioSink(nullptr)
    , sinkDevice(nullptr)
{
    // UI setup
    central = new QWidget(this);
    auto* vlay = new QVBoxLayout(central);
    auto* hlay = new QHBoxLayout();
    btnStream = new QPushButton("Stream", central);
    btnMic    = new QPushButton("Microphone", central);
    status    = new QLabel("Idle", central);

    hlay->addWidget(btnStream);
    hlay->addWidget(btnMic);
    vlay->addLayout(hlay);
    vlay->addWidget(status);
    setCentralWidget(central);

    // Configurar formato de audio para micrófono
    AudioConfig micConfig;
    micConfig.sampleRate       = 48000;
    micConfig.channelCount     = 1;
    micConfig.sampleFormat     = QAudioFormat::Int16;
    micConfig.bufferSize       = 4096;
    micConfig.useDefaultDevice = true;
    micConfig.fallbackToPreferred = true;

    // Guardar el formato para el sink
    m_format.setSampleRate(micConfig.sampleRate);
    m_format.setChannelCount(micConfig.channelCount);
    m_format.setSampleFormat(micConfig.sampleFormat);

    // Crear AudioReceiver con configuración personalizada
    micReceiver = new AudioReceiver(micConfig, this);
    connect(micReceiver, &AudioReceiver::chunkReady,
            this,      &MainWindow::onChunk,
            Qt::QueuedConnection);

    // Media player config
    player->setAudioOutput(audioOutput);

    // Conectar botones
    connect(btnStream, &QPushButton::clicked,
            this,      &MainWindow::switchToStream);
    connect(btnMic, &QPushButton::clicked,
            this,      &MainWindow::switchToMicrophone);
}

MainWindow::~MainWindow() {
    player->stop();
    micReceiver->stop();
    if (audioSink) audioSink->stop();
}

void MainWindow::switchToStream() {
    // Stop mic
    micReceiver->stop();
    if (audioSink) {
        audioSink->stop();
        audioSink->deleteLater();
        audioSink = nullptr;
        sinkDevice = nullptr;
    }

    // Start stream
    const QUrl url("http://stream.radioparadise.com/aac-128");
    player->setSource(url);
    player->play();
    status->setText("Playing stream");
    qDebug() << "Switched to stream";
}

void MainWindow::switchToMicrophone() {
    // Stop stream
    player->stop();

    // Stop any previous sink
    if (audioSink) {
        audioSink->stop();
        audioSink->deleteLater();
        audioSink = nullptr;
        sinkDevice = nullptr;
    }

    // Crear sink con el formato del receptor
    QAudioFormat actualFormat = micReceiver->getCurrentFormat();
    if (actualFormat.isValid()) {
        audioSink = new QAudioSink(actualFormat, this);
        qDebug() << "Usando formato del AudioReceiver:"
                 << actualFormat.sampleRate() << "Hz,"
                 << actualFormat.channelCount() << "canales";
    } else {
        audioSink = new QAudioSink(m_format, this);
        qDebug() << "Usando formato predefinido";
    }

    sinkDevice = audioSink->start();
    if (!sinkDevice) {
        qWarning() << "No se pudo crear el dispositivo de salida de audio";
        status->setText("Error: no se pudo iniciar salida de audio");
        return;
    }

    // Iniciar captura del micrófono
    micReceiver->start();

    status->setText(QString("Capturing microphone - %1")
                        .arg(micReceiver->getCurrentDevice().description()));
    qDebug() << "Switched to microphone";
    qDebug() << micReceiver->getDeviceInfo();
}

void MainWindow::onChunk(const QVector<float>& samples, qint64 /*timestamp*/) {
    if (!sinkDevice || samples.isEmpty()) return;

    // Convertir cada float [-1,1] a int16 PCM
    QByteArray out;
    out.resize(samples.size() * sizeof(qint16));
    qint16* dst = reinterpret_cast<qint16*>(out.data());

    for (int i = 0; i < samples.size(); ++i) {
        float v = samples[i];
        if (v < -1.0f) v = -1.0f;
        if (v >  1.0f) v =  1.0f;
        dst[i] = static_cast<qint16>(v * 32767.0f);
    }

    // Escribir al sink
    qint64 written = sinkDevice->write(out);
    if (written != out.size()) {
        qWarning() << "No se pudieron escribir todos los datos al sink:"
                   << written << "de" << out.size() << "bytes";
    }
}
