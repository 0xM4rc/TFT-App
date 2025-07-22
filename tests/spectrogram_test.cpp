#include <QApplication>
#include <QTest>
#include <QDebug>
#include <QVector>
#include <QtMath>
#include <QTime>
#include <QElapsedTimer>
#include "../core/spectrogram_calculator.h"

class SpectrogramTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testBasicConfiguration();
    void testWindowFunctions();
    void testFrequencyBins();
    void testSineWaveDetection();
    void testMultipleFrequencies();
    void testOverlappedProcessing();
    void testErrorHandling();
    void testPerformance();
    void testWindowCalculation();
    void testWindowTypeString();

private:
    SpectrogramCalculator* calculator;

    // Función auxiliar para generar señal sinusoidal
    QVector<float> generateSineWave(float frequency, float sampleRate, int samples, float amplitude = 1.0f);

    // Función auxiliar para verificar picos de frecuencia
    bool findPeakNearFrequency(const QVector<float>& magnitudes, const QVector<float>& frequencies,
                               float targetFreq, float tolerance = 50.0f);
};

void SpectrogramTest::initTestCase()
{
    qDebug() << "Iniciando tests del SpectrogramCalculator...";

    // Configuración por defecto
    SpectrogramConfig config;
    config.fftSize = 1024;
    config.hopSize = 512;
    config.sampleRate = 44100;
    config.windowType = WindowType::Hann;

    calculator = new SpectrogramCalculator(config);
    QVERIFY(calculator != nullptr);
}

void SpectrogramTest::cleanupTestCase()
{
    delete calculator;
    qDebug() << "Tests completados.";
}

void SpectrogramTest::testBasicConfiguration()
{
    qDebug() << "Test: Configuración básica";

    // Configurar parámetros básicos
    SpectrogramConfig config;
    config.fftSize = 2048;
    config.hopSize = 1024;
    config.sampleRate = 48000;
    config.windowType = WindowType::Hamming;

    calculator->setConfig(config);

    // Verificar configuración
    SpectrogramConfig currentConfig = calculator->getConfig();
    QCOMPARE(currentConfig.fftSize, 2048);
    QCOMPARE(currentConfig.hopSize, 1024);
    QCOMPARE(currentConfig.sampleRate, 48000);
    QCOMPARE(currentConfig.windowType, WindowType::Hamming);

    qDebug() << "✓ Configuración básica correcta";
}

void SpectrogramTest::testWindowFunctions()
{
    qDebug() << "Test: Funciones de ventana";

    SpectrogramConfig config = calculator->getConfig();
    config.fftSize = 1024;

    // Probar diferentes tipos de ventana
    QVector<WindowType> windowTypes = {
        WindowType::Rectangular,
        WindowType::Hann,
        WindowType::Hamming,
        WindowType::Blackman,
        WindowType::Bartlett,
        WindowType::Kaiser,
        WindowType::BlackmanHarris,
        WindowType::Gaussian
    };

    for (WindowType windowType : windowTypes) {
        config.windowType = windowType;
        calculator->setConfig(config);

        // Verificar que la ventana se aplica correctamente
        QVector<float> testSignal(1024, 1.0f);
        SpectrogramFrame frame = calculator->calculateFrame(testSignal);
        QVERIFY(!frame.magnitudes.isEmpty());

        QString windowName = SpectrogramCalculator::windowTypeToString(windowType);
        qDebug() << "✓ Ventana" << windowName << "funcionando";
    }
}

void SpectrogramTest::testFrequencyBins()
{
    qDebug() << "Test: Bins de frecuencia";

    SpectrogramConfig config;
    config.fftSize = 1024;
    config.sampleRate = 44100;
    calculator->setConfig(config);

    // Obtener bins de frecuencia
    QVector<float> frequencies = calculator->getFrequencyBins();

    // Verificar número de bins (FFT/2 + 1 para espectro real)
    int expectedBins = config.fftSize / 2 + 1;
    QCOMPARE(frequencies.size(), expectedBins);

    // Verificar que las frecuencias están en orden ascendente
    for (int i = 1; i < frequencies.size(); ++i) {
        QVERIFY(frequencies[i] > frequencies[i-1]);
    }

    // Verificar frecuencia máxima (Nyquist)
    float maxFreq = frequencies.last();
    float expectedMaxFreq = config.sampleRate / 2.0f;
    QCOMPARE(maxFreq, expectedMaxFreq);

    qDebug() << "✓ Bins de frecuencia correctos:" << frequencies.size() << "bins";
}

void SpectrogramTest::testSineWaveDetection()
{
    qDebug() << "Test: Detección de onda sinusoidal";

    SpectrogramConfig config;
    config.fftSize = 1024;
    config.sampleRate = 44100;
    config.windowType = WindowType::Hann;
    config.logScale = false; // Usar escala lineal para facilitar la detección
    calculator->setConfig(config);

    // Generar señal sinusoidal de 1000 Hz
    float testFreq = 1000.0f;
    QVector<float> sineWave = generateSineWave(testFreq, 44100, 1024);

    // Procesar señal
    SpectrogramFrame frame = calculator->calculateFrame(sineWave);
    QVERIFY(!frame.magnitudes.isEmpty());
    QVERIFY(!frame.frequencies.isEmpty());

    // Verificar que hay un pico cerca de 1000 Hz
    bool peakFound = findPeakNearFrequency(frame.magnitudes, frame.frequencies, testFreq, 50.0f);
    QVERIFY(peakFound);

    qDebug() << "✓ Detección de sinusoidal a" << testFreq << "Hz correcta";
}

void SpectrogramTest::testMultipleFrequencies()
{
    qDebug() << "Test: Múltiples frecuencias";

    SpectrogramConfig config;
    config.fftSize = 2048;
    config.sampleRate = 44100;
    config.windowType = WindowType::Hann;
    config.logScale = false;
    calculator->setConfig(config);

    // Generar señal con múltiples frecuencias
    QVector<float> freq1 = generateSineWave(440.0f, 44100, 2048, 0.5f);  // A4
    QVector<float> freq2 = generateSineWave(880.0f, 44100, 2048, 0.3f);  // A5
    QVector<float> freq3 = generateSineWave(1760.0f, 44100, 2048, 0.2f); // A6

    // Combinar frecuencias
    QVector<float> combinedSignal(2048);
    for (int i = 0; i < 2048; ++i) {
        combinedSignal[i] = freq1[i] + freq2[i] + freq3[i];
    }

    // Procesar señal combinada
    SpectrogramFrame frame = calculator->calculateFrame(combinedSignal);

    // Verificar que se detectan todas las frecuencias
    QVERIFY(findPeakNearFrequency(frame.magnitudes, frame.frequencies, 440.0f, 20.0f));
    QVERIFY(findPeakNearFrequency(frame.magnitudes, frame.frequencies, 880.0f, 20.0f));
    QVERIFY(findPeakNearFrequency(frame.magnitudes, frame.frequencies, 1760.0f, 20.0f));

    qDebug() << "✓ Detección de múltiples frecuencias correcta";
}

void SpectrogramTest::testOverlappedProcessing()
{
    qDebug() << "Test: Procesamiento con overlap";

    SpectrogramConfig config;
    config.fftSize = 1024;
    config.hopSize = 512;  // 50% overlap
    config.sampleRate = 44100;
    config.windowType = WindowType::Hann;
    config.logScale = false; // Usar escala lineal para mejor detección
    calculator->setConfig(config);

    // Generar señal más larga y más estable
    QVector<float> longSignal = generateSineWave(1000.0f, 44100, 6144, 1.0f);

    // Procesar en frames con overlap
    QVector<SpectrogramFrame> frames = calculator->processOverlapped(longSignal);

    QVERIFY(frames.size() > 1);
    qDebug() << "Frames generados:" << frames.size();

    // Verificar que la mayoría de frames detectan el pico
    int framesWithPeak = 0;
    for (int i = 0; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        if (findPeakNearFrequency(frame.magnitudes, frame.frequencies, 1000.0f, 50.0f)) {
            framesWithPeak++;
        }
    }

    // Aceptar si al menos el 80% de los frames detectan el pico
    float detectionRate = static_cast<float>(framesWithPeak) / frames.size();
    qDebug() << "Tasa de detección:" << detectionRate * 100 << "%";

    QVERIFY(detectionRate >= 0.8f); // Al menos 80% de detección

    qDebug() << "✓ Procesamiento con overlap:" << frames.size() << "frames,"
             << framesWithPeak << "con pico detectado";
}

void SpectrogramTest::testErrorHandling()
{
    qDebug() << "Test: Manejo de errores";

    // Test con señal vacía
    QVector<float> emptySignal;
    SpectrogramFrame result = calculator->calculateFrame(emptySignal);
    QVERIFY(result.magnitudes.isEmpty());

    // Test con configuración inválida
    SpectrogramConfig badConfig;
    badConfig.fftSize = 0;
    badConfig.sampleRate = -1;
    badConfig.hopSize = -1;

    calculator->setConfig(badConfig);

    // La clase debe corregir valores inválidos
    SpectrogramConfig correctedConfig = calculator->getConfig();
    QVERIFY(correctedConfig.fftSize > 0);
    QVERIFY(correctedConfig.sampleRate > 0);
    QVERIFY(correctedConfig.hopSize > 0);

    qDebug() << "✓ Manejo de errores correcto";
}

void SpectrogramTest::testPerformance()
{
    qDebug() << "Test: Rendimiento";

    SpectrogramConfig config;
    config.fftSize = 2048;
    config.sampleRate = 44100;
    config.windowType = WindowType::Hann;
    calculator->setConfig(config);

    // Generar señal de test
    QVector<float> testSignal = generateSineWave(1000.0f, 44100, 2048);

    // Medir tiempo de procesamiento
    QElapsedTimer timer;
    timer.start();

    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        calculator->calculateFrame(testSignal);
    }

    qint64 elapsed = timer.elapsed();
    double avgTime = static_cast<double>(elapsed) / iterations;

    qDebug() << "✓ Rendimiento:" << avgTime << "ms por frame";
    qDebug() << "✓ Throughput:" << (1000.0 / avgTime) << "frames/segundo";

    // Verificar que el procesamiento es suficientemente rápido
    // Para audio en tiempo real necesitamos procesar más rápido que el hop time
    double hopTimeMs = (512.0 / 44100.0) * 1000.0;  // Tiempo del hop en ms
    QVERIFY(avgTime < hopTimeMs);
}

void SpectrogramTest::testWindowCalculation()
{
    qDebug() << "Test: Cálculo de ventanas";

    int windowSize = 1024;

    // Probar el cálculo estático de ventanas
    QVector<WindowType> windowTypes = {
        WindowType::Rectangular,
        WindowType::Hann,
        WindowType::Hamming,
        WindowType::Blackman,
        WindowType::Bartlett,
        WindowType::Kaiser,
        WindowType::BlackmanHarris,
        WindowType::Gaussian
    };

    for (WindowType type : windowTypes) {
        QVector<float> window = SpectrogramCalculator::calculateWindow(type, windowSize);

        QCOMPARE(window.size(), windowSize);

        // Verificar que los valores están en un rango razonable
        for (float val : window) {
            QVERIFY(val >= 0.0f && val <= 1.0f);
        }

        QString windowName = SpectrogramCalculator::windowTypeToString(type);
        qDebug() << "✓ Ventana" << windowName << "calculada correctamente";
    }
}

void SpectrogramTest::testWindowTypeString()
{
    qDebug() << "Test: Conversión de tipos de ventana a string";

    QVector<WindowType> windowTypes = {
        WindowType::Rectangular,
        WindowType::Hann,
        WindowType::Hamming,
        WindowType::Blackman,
        WindowType::Bartlett,
        WindowType::Kaiser,
        WindowType::BlackmanHarris,
        WindowType::Gaussian
    };

    for (WindowType type : windowTypes) {
        QString windowName = SpectrogramCalculator::windowTypeToString(type);
        QVERIFY(!windowName.isEmpty());
        QVERIFY(windowName != "Unknown");
    }

    qDebug() << "✓ Conversión a string correcta";
}

// Funciones auxiliares
QVector<float> SpectrogramTest::generateSineWave(float frequency, float sampleRate, int samples, float amplitude)
{
    QVector<float> wave(samples);
    float phaseIncrement = 2.0f * M_PI * frequency / sampleRate;

    for (int i = 0; i < samples; ++i) {
        wave[i] = amplitude * qSin(i * phaseIncrement);
    }

    return wave;
}

bool SpectrogramTest::findPeakNearFrequency(const QVector<float>& magnitudes,
                                            const QVector<float>& frequencies,
                                            float targetFreq, float tolerance)
{
    if (magnitudes.size() != frequencies.size() || magnitudes.isEmpty()) {
        return false;
    }

    // Buscar el índice de la frecuencia más cercana
    int targetIndex = -1;
    float minDiff = std::numeric_limits<float>::max();

    for (int i = 0; i < frequencies.size(); ++i) {
        float diff = qAbs(frequencies[i] - targetFreq);
        if (diff < minDiff) {
            minDiff = diff;
            targetIndex = i;
        }
    }

    if (targetIndex == -1 || minDiff > tolerance) {
        return false;
    }

    // Buscar en un rango más amplio alrededor del índice objetivo
    int searchRange = 10; // Aumentar rango de búsqueda
    int startIndex = qMax(0, targetIndex - searchRange);
    int endIndex = qMin(magnitudes.size() - 1, targetIndex + searchRange);

    // Encontrar el valor máximo en el rango
    float maxMagnitude = 0.0f;
    float avgMagnitude = 0.0f;
    int count = 0;

    for (int i = startIndex; i <= endIndex; ++i) {
        maxMagnitude = qMax(maxMagnitude, magnitudes[i]);
        avgMagnitude += magnitudes[i];
        count++;
    }

    if (count > 0) {
        avgMagnitude /= count;
    }

    // Calcular el promedio de toda la señal para comparación
    float totalAvg = 0.0f;
    for (float mag : magnitudes) {
        totalAvg += mag;
    }
    totalAvg /= magnitudes.size();

    // Verificar que hay un pico significativo
    // El pico debe ser mayor que el promedio total y tener cierta magnitud mínima
    float threshold = qMax(0.001f, totalAvg * 2.0f);
    bool hasSignificantPeak = maxMagnitude > threshold;

    // También verificar que el pico es suficientemente prominente
    bool isProminent = maxMagnitude > avgMagnitude * 1.5f;

    return hasSignificantPeak && isProminent;
}


QTEST_MAIN(SpectrogramTest)
#include "spectrogram_test.moc"
