#include <QApplication>
#include <QVBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QWidget>
#include <QDebug>

#include "audio_db.h"
#include "dsp_worker.h"
#include "network_receiver.h"

// 1) Incluir ahora el renderer de waveform y el de espectrograma
#include "waveform_render.h"
#include "spectrogram_renderer.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /////////////////////////////
    // 1) PREPARAR BASE DE DATOS
    /////////////////////////////
    QString dbPath = "/home/m4rc/Desktop/tft-app/TFT-App/audio_capture.db";
    QDir dir(QFileInfo(dbPath).absolutePath());
    if (!dir.exists()) dir.mkpath(".");

    AudioDb audioDb(dbPath, &app);
    if (!audioDb.initialize()) {
        qCritical() << "Error inicializando la base de datos";
        return -1;
    }
    audioDb.clearDatabase();

    /////////////////////////////
    // 2) CONFIGURAR DSPWorker
    /////////////////////////////
    DSPConfig dspConfig;
    dspConfig.blockSize      = 4096;
    dspConfig.fftSize        = 1024;    // tamaño de FFT para espectro
    dspConfig.sampleRate     = 44100;
    dspConfig.enableSpectrum = true;    // activamos espectro
    dspConfig.enablePeaks    = true;

    DSPWorker dspWorker(dspConfig, &audioDb, &app);

    /////////////////////////////
    // 3) CREAR UI
    /////////////////////////////
    QWidget window;
    window.setWindowTitle("Live Waveform + Spectrogram");
    window.resize(800, 600);

    QVBoxLayout* layout = new QVBoxLayout(&window);

    // 3a) WaveformRenderer
    WaveformRenderer* wfRenderer = new WaveformRenderer;
    WaveformConfig wfCfg;
    wfCfg.blockWidth       = 4;
    wfCfg.blockSpacing     = 1;
    wfCfg.showPeaks        = true;
    wfCfg.showRMS          = false;
    wfCfg.autoScale        = true;
    wfCfg.scrolling        = true;
    wfCfg.backgroundColor  = Qt::black;
    wfCfg.peakColor        = Qt::green;
    wfCfg.rmsColor         = Qt::yellow;
    wfCfg.updateInterval   = 30;
    wfCfg.maxVisibleBlocks = 0;
    wfRenderer->setConfig(wfCfg);

    layout->addWidget(wfRenderer, /* stretch=*/1);

    // 3b) SpectrogramRenderer
    SpectrogramRenderer* specRenderer = new SpectrogramRenderer;
    SpectrogramConfig specCfg;
    specCfg.fftSize        = dspConfig.fftSize;
    specCfg.sampleRate     = dspConfig.sampleRate;
    specCfg.blockWidth     = 2;        // ancho más estrecho
    specCfg.updateInterval = 30;
    specCfg.maxColumns     = 400;      // tiempo visible
    specCfg.autoScroll     = true;
    specCfg.minDb          = -100.0f;
    specCfg.maxDb          =   0.0f;
    specRenderer->setConfig(specCfg);

    layout->addWidget(specRenderer, /* stretch=*/1);

    window.show();

    /////////////////////////////
    // 4) CONFIGURAR RECEPCIÓN RED
    /////////////////////////////
    NetworkReceiver networkReceiver(&app);
    networkReceiver.setUrl("http://stream.radioparadise.com/rock-128");

    QObject::connect(&networkReceiver, &NetworkReceiver::floatChunkReady,
                     &dspWorker, &DSPWorker::processChunk);
    QObject::connect(&networkReceiver, &NetworkReceiver::errorOccurred,
                     [&](const QString& err){
                         qCritical() << "Red:" << err;
                         app.quit();
                     });

    /////////////////////////////
    // 5) SEÑALES DEL DSP
    /////////////////////////////
    // 5a) conectar framesReady a ambos renderers
    QObject::connect(&dspWorker, &DSPWorker::framesReady,
                     wfRenderer, &WaveformRenderer::processFrames);
    QObject::connect(&dspWorker, &DSPWorker::framesReady,
                     specRenderer, &SpectrogramRenderer::processFrames);

    QObject::connect(&dspWorker, &DSPWorker::errorOccurred,
                     [&](const QString& err){
                         qCritical() << "DSP:" << err;
                         app.quit();
                     });

    QObject::connect(&dspWorker, &DSPWorker::statsUpdated,
                     [&](qint64 blocks, qint64 samples, int buf){
                         if (blocks % 50 == 0) {
                             qDebug() << "Progreso:" << blocks
                                      << "bloques," << samples
                                      << "muestras en buffer:" << buf;
                         }
                     });

    /////////////////////////////
    // 6) INICIAR STREAMING
    /////////////////////////////
    networkReceiver.start();

    return app.exec();
}
