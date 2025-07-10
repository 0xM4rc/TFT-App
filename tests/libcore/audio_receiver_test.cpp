#include <QtTest>
#include <QSignalSpy>
#include "../audio_receiver.h"   // ajusta la ruta si hace falta
#include "../ireceiver.h"

class TestAudioReceiver : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Se ejecuta una vez antes de todos los tests
    }

    void cleanupTestCase() {
        // Se ejecuta una vez tras todos los tests
    }

    void testStartStop() {
        AudioReceiver recv;
        ReceiverConfig cfg(ReceiverConfig::Audio);
        cfg.deviceId             = "";        // usar dispositivo por defecto
        cfg.sampleRate           = 16000;
        cfg.channelCount         = 1;
        cfg.sampleFormat         = QAudioFormat::Float;
        cfg.bufferSize           = 4096;
        cfg.usePreferredFormat   = false;
        cfg.fallbackToPreferred  = true;
        recv.setConfig(cfg);

        QSignalSpy spyFmt(&recv, &IReceiver::audioFormatDetected);
        QSignalSpy spyChunk(&recv, &IReceiver::chunkReady);

        recv.start();

        // Esperamos hasta 2 s por la señal de formato...
        QVERIFY(spyFmt.wait(2000));
        // ...y hasta 2 s por el primer chunk
        QVERIFY(spyChunk.wait(2000));

        // Comprobamos que el formato es el configurado
        auto fmtArgs = spyFmt.takeFirst();
        QAudioFormat fmt = fmtArgs.at(0).value<QAudioFormat>();
        QCOMPARE(fmt.sampleRate(), 16000);
        QCOMPARE(fmt.channelCount(), 1);
        QCOMPARE(fmt.sampleFormat(), QAudioFormat::Float);

        recv.stop();

        // Tras parar, no debe llegar más ningún chunk
        int before = spyChunk.count();
        QTest::qWait(500);
        QCOMPARE(spyChunk.count(), before);
    }

    void testInvalidDevice() {
        AudioReceiver recv;
        ReceiverConfig cfg(ReceiverConfig::Audio);
        cfg.deviceId = "DispositivoInexistenteXYZ";
        recv.setConfig(cfg);

        QSignalSpy spyErr(&recv, &IReceiver::errorOccurred);
        recv.start();

        // Debe emitir errorOccurred en menos de 500 ms
        QVERIFY(spyErr.wait(500));
        QString msg = spyErr.takeFirst().at(0).toString();
        QVERIFY(!msg.isEmpty());
    }
};

QTEST_MAIN(TestAudioReceiver)
#include "audio_receiver_test.moc"
