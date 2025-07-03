// #include <QApplication>
// #include <QTimer>
// #include <QDebug>
// #include <QAudioSink>
// #include <QAudioFormat>
// #include <QAudioDevice>
// #include <QMediaDevices>
// #include <QBuffer>
// #include <QDir>
// #include <QFileInfo>

// // Tus headers existentes
// #include "mainwindow.h"
// #include "network_receiver.h"
// #include "audio_db.h"
// #include "dsp_worker.h"

// // Helper para convertir float32→int16
// static QByteArray convertFloat32ToInt16(const QByteArray& float32Data) {
//     if (float32Data.size() % sizeof(float) != 0) {
//         qWarning() << "Tamaño de datos inválido para conversión float32";
//         return {};
//     }
//     int sampleCount = float32Data.size() / sizeof(float);
//     const float* floatSamples = reinterpret_cast<const float*>(float32Data.constData());
//     QByteArray int16Data(sampleCount * sizeof(qint16), 0);
//     qint16* int16Samples = reinterpret_cast<qint16*>(int16Data.data());
//     for (int i = 0; i < sampleCount; ++i) {
//         float s = qBound(-1.0f, floatSamples[i], 1.0f);
//         int16Samples[i] = static_cast<qint16>(s * 32767.0f);
//     }
//     return int16Data;
// }

// int main(int argc, char *argv[])
// {
//     QApplication app(argc, argv);

//     // 1) Inicializar Base de Datos
//     QString dbPath = "/home/m4rc/Desktop/tft-app/TFT-App/audio_capture.db";
//     QDir dir(QFileInfo(dbPath).absolutePath());
//     if (!dir.exists()) dir.mkpath(".");
//     AudioDb audioDb(dbPath, &app);
//     if (!audioDb.initialize()) {
//         qCritical() << "Error inicializando la base de datos";
//         return -1;
//     }
//     audioDb.clearDatabase();

//     // 2) Configurar DSPWorker
//     // Usamos el constructor que recibe blockSize y fftSize (el sampleRate por defecto es 44100)
//     DSPConfig dspConfig(4096, 1024);

//     // Si quieres un sampleRate distinto, podrías hacer:
//     // DSPConfig dspConfig(4096, 1024, 48000);

//     // Ahora ajustamos los flags según tus necesidades
//     dspConfig.enableSpectrum = false;  // deshabilitamos cálculo de espectro
//     dspConfig.enablePeaks    = true;   // habilitamos detección de picos

//     DSPWorker dspWorker(dspConfig, &audioDb, &app);

//     // 3) Configurar NetworkReceiver
//     NetworkReceiver networkReceiver(&app);
//     networkReceiver.setUrl("http://stream.radioparadise.com/rock-128");

//     // 4) Conexiones de señal/slot a lambdas
//     QObject::connect(&networkReceiver, &NetworkReceiver::floatChunkReady,
//                      &dspWorker, &DSPWorker::processChunk);
//     QObject::connect(&networkReceiver, &NetworkReceiver::errorOccurred,
//                      [&](const QString& err){ qCritical() << "Red:" << err; QCoreApplication::quit(); });
//     QObject::connect(&dspWorker, &DSPWorker::errorOccurred,
//                      [&](const QString& err){ qCritical() << "DSP:" << err; QCoreApplication::quit(); });
//     QObject::connect(&dspWorker, &DSPWorker::statsUpdated,
//                      [&](qint64 blocks, qint64 samples, int buf){
//                          if (blocks % 50 == 0)
//                              qDebug() << "Progreso:" << blocks << "bloques," << samples << "muestras, buf:" << buf;
//                      });

//     // 5) Timer para detener captura y arrancar reproducción
//     QTimer captureTimer;
//     captureTimer.setSingleShot(true);
//     captureTimer.setInterval(3600000);
//     QObject::connect(&captureTimer, &QTimer::timeout, [&](){
//         qDebug() << "=== Deteniendo captura ===";
//         networkReceiver.stop();
//         dspWorker.flushResidual();
//         qDebug() << audioDb.getStatistics();

//         // Preparar datos de reproducción
//         QList<QByteArray> blocks = audioDb.getAllAudioBlocks();
//         if (blocks.isEmpty()) {
//             qWarning() << "Sin audio en BD";
//             app.quit();
//             return;
//         }

//         // Convertir y concatenar
//         QByteArray playData;
//         for (auto &b : blocks) {
//             QByteArray c = convertFloat32ToInt16(b);
//             if (!c.isEmpty()) playData.append(c);
//         }
//         if (playData.isEmpty()) {
//             qWarning() << "Error en conversión";
//             app.quit();
//             return;
//         }

//         // Buffer de reproducción
//         QBuffer* buffer = new QBuffer(&app);
//         buffer->setData(playData);
//         buffer->open(QIODevice::ReadOnly);

//         // Configurar formato de audio
//         QAudioFormat fmt;
//         fmt.setSampleRate(44100);
//         fmt.setChannelCount(2);
//         fmt.setSampleFormat(QAudioFormat::Int16);

//         QAudioDevice outDev = QMediaDevices::defaultAudioOutput();
//         if (!outDev.isFormatSupported(fmt)) {
//             qWarning() << "Formato no soportado, usando preferido";
//             fmt = outDev.preferredFormat();
//         }

//         QAudioSink* sink = new QAudioSink(outDev, fmt, &app);
//         QObject::connect(sink, &QAudioSink::stateChanged, [&](QAudio::State st){
//             if (st == QAudio::IdleState) qDebug() << "Reproducción completada";
//         });
//         sink->start(buffer);
//         qDebug() << "Reproduciendo... tamaño total:" << playData.size();

//         // Salir tras 15s de reproducción
//         QTimer::singleShot(15000, &app, &QCoreApplication::quit);
//     });

//     // 6) Arrancar captura tras 0.5s
//     QTimer::singleShot(500, [&](){
//         qDebug() << "=== Iniciando captura de audio ===";
//         networkReceiver.start();
//         captureTimer.start();
//     });

//     MainWindow w(&audioDb);
//     w.resize(800, 600);
//     w.show();

//     return app.exec();

//     return app.exec();
// }
