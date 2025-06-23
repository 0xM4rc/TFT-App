#include <QApplication>
#include <QVBoxLayout>
#include <QWidget>
#include "include/audio_manager.h"
#include "include/simple_waveform.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Ventana principal
    QWidget window;
    window.setWindowTitle("Audio Waveform Viewer (Simple)");
    window.resize(800, 300);

    QVBoxLayout *layout = new QVBoxLayout(&window);

    // Widget simplificado de waveform
    SimpleWaveformWidget *waveform = new SimpleWaveformWidget;
    layout->addWidget(waveform);

    // AudioManager
    AudioManager *audioManager = new AudioManager(&window);
    audioManager->setWaveformDuration(5.0);  // 5 segundos de buffer interno
    audioManager->setWaveformSamples(2048);  // resolución de 2048 muestras

    // Conecta la señal de visualización a un lambda que extrae viz.waveform
    QObject::connect(audioManager, &AudioManager::visualizationDataReady,
                     waveform,
                     [waveform](const VisualizationData &viz){
                         waveform->updateWaveform(viz.waveform);
                     });

    // Inicia la fuente de red y el procesamiento
    audioManager->fetchURL(QUrl("http://stream.radioparadise.com/rock-128"));
    audioManager->startProcessing();

    window.show();
    return app.exec();
}
