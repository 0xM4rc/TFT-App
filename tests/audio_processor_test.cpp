#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QVector>
#include <QAudioFormat>
#include <memory>
#include <cmath>
#include "../include/audio_processor.h"

class AudioProcessorTest : public QObject
{
    Q_OBJECT

private slots:
    // AudioBuffer Tests
    void testAudioBufferConstruction();
    void testAudioBufferAppendData();
    void testAudioBufferCircularBuffer();
    void testAudioBufferGetLastSamples();
    void testAudioBufferGetAllSamples();
    void testAudioBufferClear();
    void testAudioBufferResize();
    void testAudioBufferThreadSafety();

    // FFTProcessor Tests
    void testFFTProcessorConstruction();
    void testFFTProcessorSetFFTSize();
    void testFFTProcessorInvalidFFTSize();
    void testFFTProcessorWindowTypes();
    void testFFTProcessorFFTExecution();
    void testFFTProcessorDecibelsConversion();
    void testFFTProcessorWindowApplication();
    void testFFTProcessorMemoryManagement();

    // AudioProcessor Tests
    void testAudioProcessorConstruction();
    void testAudioProcessorConfiguration();
    void testAudioProcessorProcessing();
    void testAudioProcessorWaveformProcessing();
    void testAudioProcessorSpectrumProcessing();
    void testAudioProcessorSpectrogramUpdate();
    void testAudioProcessorSmoothing();
    void testAudioProcessorFrequencyRange();
    void testAudioProcessorReset();
    void testAudioProcessorErrorHandling();
    void testAudioProcessorThreadSafety();

    // Integration Tests
    void testCompleteProcessingPipeline();
    void testRealTimeProcessing();
    void testPerformanceCharacteristics();

private:
    // Helper functions
    QVector<float> generateSineWave(float frequency, float sampleRate, int samples, float amplitude = 1.0f);
    QVector<float> generateWhiteNoise(int samples, float amplitude = 1.0f);
    QVector<float> generateChirpSignal(float startFreq, float endFreq, float sampleRate, int samples);
    QAudioFormat createStandardFormat(int sampleRate = 44100, int channels = 2);
    AudioConfiguration createTestConfiguration();
    void verifySpectrumPeak(const QVector<float>& spectrum, float expectedFreq, float sampleRate, float tolerance = 0.1f);
};

// ============================================================================
// Helper Functions Implementation
// ============================================================================

QVector<float> AudioProcessorTest::generateSineWave(float frequency, float sampleRate, int samples, float amplitude)
{
    QVector<float> wave(samples);
    float phaseIncrement = 2.0f * M_PI * frequency / sampleRate;

    for (int i = 0; i < samples; ++i) {
        wave[i] = amplitude * sin(i * phaseIncrement);
    }

    return wave;
}

QVector<float> AudioProcessorTest::generateWhiteNoise(int samples, float amplitude)
{
    QVector<float> noise(samples);

    for (int i = 0; i < samples; ++i) {
        noise[i] = amplitude * (2.0f * (float)rand() / RAND_MAX - 1.0f);
    }

    return noise;
}

QVector<float> AudioProcessorTest::generateChirpSignal(float startFreq, float endFreq, float sampleRate, int samples)
{
    QVector<float> chirp(samples);
    float duration = samples / sampleRate;
    float k = (endFreq - startFreq) / duration;

    for (int i = 0; i < samples; ++i) {
        float t = i / sampleRate;
        float instantFreq = startFreq + k * t;
        chirp[i] = sin(2.0f * M_PI * startFreq * t + M_PI * k * t * t);
    }

    return chirp;
}

QAudioFormat AudioProcessorTest::createStandardFormat(int sampleRate, int channels)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);
    return format;
}

AudioConfiguration AudioProcessorTest::createTestConfiguration()
{
    AudioConfiguration config;
    config.fftSize = 1024;
    config.overlap = 512;
    config.spectrogramHistory = 100;
    config.waveformBufferSize = 4096;
    config.windowType = 1; // Hanning
    config.minFrequency = 20.0f;
    config.maxFrequency = 20000.0f;
    return config;
}

void AudioProcessorTest::verifySpectrumPeak(const QVector<float>& spectrum, float expectedFreq, float sampleRate, float tolerance)
{
    float binFreq = sampleRate / (2.0f * (spectrum.size() - 1));
    int expectedBin = (int)(expectedFreq / binFreq);

    // Find peak in expected region
    int searchStart = qMax(0, expectedBin - 2);
    int searchEnd = qMin(spectrum.size() - 1, expectedBin + 2);

    float maxValue = -1000.0f;
    int maxBin = -1;

    for (int i = searchStart; i <= searchEnd; ++i) {
        if (spectrum[i] > maxValue) {
            maxValue = spectrum[i];
            maxBin = i;
        }
    }

    QVERIFY(maxBin != -1);
    QVERIFY(abs(maxBin - expectedBin) <= 2); // Within 2 bins tolerance
}

// ============================================================================
// AudioBuffer Tests
// ============================================================================

void AudioProcessorTest::testAudioBufferConstruction()
{
    AudioBuffer buffer(1024);

    QCOMPARE(buffer.getCurrentSize(), 0);
    QCOMPARE(buffer.getMaxSize(), 1024);
    QVERIFY(buffer.getAllSamples().isEmpty());
}

void AudioProcessorTest::testAudioBufferAppendData()
{
    AudioBuffer buffer(100);
    QVector<float> testData = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    buffer.append(testData);

    QCOMPARE(buffer.getCurrentSize(), 5);
    QVector<float> result = buffer.getAllSamples();
    QCOMPARE(result.size(), 5);
    QCOMPARE(result, testData);
}

void AudioProcessorTest::testAudioBufferCircularBuffer()
{
    AudioBuffer buffer(5);

    // Fill buffer
    QVector<float> data1 = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    buffer.append(data1);
    QCOMPARE(buffer.getCurrentSize(), 5);

    // Overflow buffer
    QVector<float> data2 = {6.0f, 7.0f, 8.0f};
    buffer.append(data2);
    QCOMPARE(buffer.getCurrentSize(), 5);

    // Should contain last 5 values: 4, 5, 6, 7, 8
    QVector<float> result = buffer.getAllSamples();
    QVector<float> expected = {4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    QCOMPARE(result, expected);
}

void AudioProcessorTest::testAudioBufferGetLastSamples()
{
    AudioBuffer buffer(10);
    QVector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    buffer.append(data);

    // Get last 3 samples
    QVector<float> last3 = buffer.getLastSamples(3);
    QVector<float> expected = {5.0f, 6.0f, 7.0f};
    QCOMPARE(last3, expected);

    // Get more samples than available
    QVector<float> lastAll = buffer.getLastSamples(10);
    QCOMPARE(lastAll, data);

    // Get 0 samples
    QVector<float> lastNone = buffer.getLastSamples(0);
    QVERIFY(lastNone.isEmpty());
}

void AudioProcessorTest::testAudioBufferGetAllSamples()
{
    AudioBuffer buffer(10);
    QVector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    buffer.append(data);

    QVector<float> result = buffer.getAllSamples();
    QCOMPARE(result, data);
}

void AudioProcessorTest::testAudioBufferClear()
{
    AudioBuffer buffer(10);
    QVector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
    buffer.append(data);

    buffer.clear();

    QCOMPARE(buffer.getCurrentSize(), 0);
    QVERIFY(buffer.getAllSamples().isEmpty());
}

void AudioProcessorTest::testAudioBufferResize()
{
    AudioBuffer buffer(10);
    QVector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f};
    buffer.append(data);

    // Resize to smaller
    buffer.setMaxSize(5);
    QCOMPARE(buffer.getMaxSize(), 5);
    QCOMPARE(buffer.getCurrentSize(), 5);

    // Resize to larger
    buffer.setMaxSize(15);
    QCOMPARE(buffer.getMaxSize(), 15);
    QCOMPARE(buffer.getCurrentSize(), 5);
}

void AudioProcessorTest::testAudioBufferThreadSafety()
{
    AudioBuffer buffer(1000);

    // This is a basic test - in real scenarios you'd use QThread
    QVector<float> data1 = generateSineWave(440.0f, 44100.0f, 100);
    QVector<float> data2 = generateSineWave(880.0f, 44100.0f, 100);

    buffer.append(data1);
    buffer.append(data2);

    QCOMPARE(buffer.getCurrentSize(), 200);
}

// ============================================================================
// FFTProcessor Tests
// ============================================================================

void AudioProcessorTest::testFFTProcessorConstruction()
{
    FFTProcessor processor(1024);

    QCOMPARE(processor.getFFTSize(), 1024);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Hanning);
}

void AudioProcessorTest::testFFTProcessorSetFFTSize()
{
    FFTProcessor processor(512);

    processor.setFFTSize(1024);
    QCOMPARE(processor.getFFTSize(), 1024);

    processor.setFFTSize(2048);
    QCOMPARE(processor.getFFTSize(), 2048);
}

void AudioProcessorTest::testFFTProcessorInvalidFFTSize()
{
    FFTProcessor processor(1024);

    // Test invalid sizes
    processor.setFFTSize(100);  // Not power of 2
    QCOMPARE(processor.getFFTSize(), 1024); // Should remain unchanged

    processor.setFFTSize(32);   // Too small
    QCOMPARE(processor.getFFTSize(), 1024); // Should remain unchanged

    processor.setFFTSize(16384); // Too large
    QCOMPARE(processor.getFFTSize(), 1024); // Should remain unchanged
}

void AudioProcessorTest::testFFTProcessorWindowTypes()
{
    FFTProcessor processor(1024);

    processor.setWindowType(FFTProcessor::Rectangle);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Rectangle);

    processor.setWindowType(FFTProcessor::Hamming);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Hamming);

    processor.setWindowType(FFTProcessor::Blackman);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Blackman);
}

void AudioProcessorTest::testFFTProcessorFFTExecution()
{
    FFTProcessor processor(1024);

    // Generate test signal: 440 Hz sine wave
    QVector<float> input = generateSineWave(440.0f, 44100.0f, 1024);
    QVector<float> output;

    bool success = processor.processFFT(input, output);

    QVERIFY(success);
    QCOMPARE(output.size(), 513); // N/2 + 1 for real FFT

    // Verify peak at expected frequency
    verifySpectrumPeak(output, 440.0f, 44100.0f);
}

void AudioProcessorTest::testFFTProcessorDecibelsConversion()
{
    QVector<float> linearData = {1.0f, 0.1f, 0.01f, 0.001f};
    QVector<float> dbData = linearData;

    FFTProcessor::convertToDecibels(dbData, -80.0f);

    QCOMPARE(dbData[0], 0.0f);      // 20*log10(1.0) = 0 dB
    QVERIFY(qAbs(dbData[1] - (-20.0f)) < 0.1f); // 20*log10(0.1) = -20 dB
    QVERIFY(qAbs(dbData[2] - (-40.0f)) < 0.1f); // 20*log10(0.01) = -40 dB
    QVERIFY(qAbs(dbData[3] - (-60.0f)) < 0.1f); // 20*log10(0.001) = -60 dB
}

void AudioProcessorTest::testFFTProcessorWindowApplication()
{
    FFTProcessor processor(1024);
    QVector<float> data(1024, 1.0f); // All ones

    processor.setWindowType(FFTProcessor::Hanning);
    processor.applyWindow(data);

    // Hanning window should be 0 at edges, max in middle
    QVERIFY(data[0] < 0.1f);
    QVERIFY(data[1023] < 0.1f);
    QVERIFY(data[512] > 0.9f);
}

void AudioProcessorTest::testFFTProcessorMemoryManagement()
{
    // Test multiple resizes to check for memory leaks
    FFTProcessor processor(512);

    for (int size = 64; size <= 4096; size *= 2) {
        processor.setFFTSize(size);
        QCOMPARE(processor.getFFTSize(), size);

        // Test processing still works
        QVector<float> input = generateSineWave(440.0f, 44100.0f, size);
        QVector<float> output;
        QVERIFY(processor.processFFT(input, output));
    }
}

// ============================================================================
// AudioProcessor Tests
// ============================================================================

void AudioProcessorTest::testAudioProcessorConstruction()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);

    QCOMPARE(processor.getFFTSize(), 1024);
    QCOMPARE(processor.getWaveformBufferSize(), 4096);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Hanning);

    float minFreq, maxFreq;
    processor.getFrequencyRange(minFreq, maxFreq);
    QCOMPARE(minFreq, 20.0f);
    QCOMPARE(maxFreq, 20000.0f);
}

void AudioProcessorTest::testAudioProcessorConfiguration()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);

    // Test FFT size change
    processor.setFFTSize(2048);
    QCOMPARE(processor.getFFTSize(), 2048);

    // Test overlap change
    processor.setOverlap(1024);
    // No direct getter for overlap, but should not crash

    // Test spectrogram history
    processor.setSpectrogramHistory(50);
    // No direct getter, but should not crash

    // Test window type
    processor.setWindowType(FFTProcessor::Hamming);
    QCOMPARE(processor.getWindowType(), FFTProcessor::Hamming);

    // Test waveform buffer size
    processor.setWaveformBufferSize(8192);
    QCOMPARE(processor.getWaveformBufferSize(), 8192);

    // Test frequency range
    processor.setFrequencyRange(50.0f, 15000.0f);
    float minFreq, maxFreq;
    processor.getFrequencyRange(minFreq, maxFreq);
    QCOMPARE(minFreq, 50.0f);
    QCOMPARE(maxFreq, 15000.0f);
}

void AudioProcessorTest::testAudioProcessorProcessing()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Generate test signal
    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 2048);
    VisualizationData vizData;

    bool success = processor.processAudioData(samples, format, vizData);

    QVERIFY(success);
    QVERIFY(!vizData.waveform.isEmpty());
    QVERIFY(!vizData.spectrum.isEmpty());
    QCOMPARE(vizData.sampleRate, 44100);
    QCOMPARE(vizData.channels, 2);
    QVERIFY(vizData.timestamp > 0);
}

void AudioProcessorTest::testAudioProcessorWaveformProcessing()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 1024);
    VisualizationData vizData;

    processor.processAudioData(samples, format, vizData);

    QVERIFY(!vizData.waveform.isEmpty());
    QVERIFY(vizData.waveform.size() <= 512); // Target waveform size

    // Check that waveform contains reasonable values
    bool hasValidData = false;
    for (float sample : vizData.waveform) {
        if (qAbs(sample) > 0.1f) {
            hasValidData = true;
            break;
        }
    }
    QVERIFY(hasValidData);
}

void AudioProcessorTest::testAudioProcessorSpectrumProcessing()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Generate enough samples for FFT processing
    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 2048);
    VisualizationData vizData;

    processor.processAudioData(samples, format, vizData);

    QVERIFY(!vizData.spectrum.isEmpty());
    QCOMPARE(vizData.spectrum.size(), 513); // FFT size/2 + 1

    // Verify peak at 440 Hz
    verifySpectrumPeak(vizData.spectrum, 440.0f, 44100.0f);
}

void AudioProcessorTest::testAudioProcessorSpectrogramUpdate()
{
    AudioConfiguration config = createTestConfiguration();
    config.spectrogramHistory = 5;
    AudioProcessor processor(config);
    processor.applyConfiguration(config);

    QAudioFormat format = createStandardFormat();

    // Process multiple frames
    for (int i = 0; i < 10; ++i) {
        QVector<float> samples = generateSineWave(440.0f + i * 100.0f, 44100.0f, 2048);
        VisualizationData vizData;
        processor.processAudioData(samples, format, vizData);

        // Spectrogram should not exceed history limit
        QVERIFY(vizData.spectrogram.size() <= 5);
    }
}

void AudioProcessorTest::testAudioProcessorSmoothing()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Process first frame
    QVector<float> samples1 = generateSineWave(440.0f, 44100.0f, 2048);
    VisualizationData vizData1;
    processor.processAudioData(samples1, format, vizData1);

    // Process second frame with different frequency
    QVector<float> samples2 = generateSineWave(880.0f, 44100.0f, 2048);
    VisualizationData vizData2;
    processor.processAudioData(samples2, format, vizData2);

    // Spectrums should be different due to different frequencies
    QVERIFY(vizData1.spectrum != vizData2.spectrum);
}

void AudioProcessorTest::testAudioProcessorFrequencyRange()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);

    // Test valid range
    processor.setFrequencyRange(100.0f, 10000.0f);
    float minFreq, maxFreq;
    processor.getFrequencyRange(minFreq, maxFreq);
    QCOMPARE(minFreq, 100.0f);
    QCOMPARE(maxFreq, 10000.0f);

    // Test invalid range (should not change)
    processor.setFrequencyRange(10000.0f, 100.0f); // max < min
    processor.getFrequencyRange(minFreq, maxFreq);
    QCOMPARE(minFreq, 100.0f); // Should remain unchanged
    QCOMPARE(maxFreq, 10000.0f);
}

void AudioProcessorTest::testAudioProcessorReset()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Process some data
    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 2048);
    VisualizationData vizData;
    processor.processAudioData(samples, format, vizData);

    // Reset processor
    processor.reset();

    // Process again - should work normally
    processor.processAudioData(samples, format, vizData);
    QVERIFY(!vizData.waveform.isEmpty());
}

void AudioProcessorTest::testAudioProcessorErrorHandling()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    QSignalSpy errorSpy(&processor, &AudioProcessor::processingError);

    // Test with empty samples
    QVector<float> emptySamples;
    VisualizationData vizData;
    bool success = processor.processAudioData(emptySamples, format, vizData);
    QVERIFY(!success);

    // Test with disabled processor
    processor.setEnabled(false);
    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 1024);
    success = processor.processAudioData(samples, format, vizData);
    QVERIFY(!success);

    processor.setEnabled(true);
}

void AudioProcessorTest::testAudioProcessorThreadSafety()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Test concurrent configuration changes and processing
    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 2048);
    VisualizationData vizData;

    // Change configuration while processing
    processor.processAudioData(samples, format, vizData);
    processor.setFFTSize(2048);
    processor.processAudioData(samples, format, vizData);
    processor.setWindowType(FFTProcessor::Hamming);
    processor.processAudioData(samples, format, vizData);

    QVERIFY(!vizData.spectrum.isEmpty());
}

// ============================================================================
// Integration Tests
// ============================================================================

void AudioProcessorTest::testCompleteProcessingPipeline()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Test with various signal types
    struct TestCase {
        QString name;
        QVector<float> samples;
        float expectedPeakFreq;
    };

    QVector<TestCase> testCases = {
        {"440Hz Sine", generateSineWave(440.0f, 44100.0f, 2048), 440.0f},
        {"1000Hz Sine", generateSineWave(1000.0f, 44100.0f, 2048), 1000.0f},
        {"White Noise", generateWhiteNoise(2048), -1.0f} // No specific peak expected
    };

    for (const auto& testCase : testCases) {
        VisualizationData vizData;
        bool success = processor.processAudioData(testCase.samples, format, vizData);

        QVERIFY2(success, qPrintable(QString("Failed processing %1").arg(testCase.name)));
        QVERIFY2(!vizData.waveform.isEmpty(), qPrintable(QString("Empty waveform for %1").arg(testCase.name)));
        QVERIFY2(!vizData.spectrum.isEmpty(), qPrintable(QString("Empty spectrum for %1").arg(testCase.name)));

        if (testCase.expectedPeakFreq > 0) {
            verifySpectrumPeak(vizData.spectrum, testCase.expectedPeakFreq, 44100.0f);
        }
    }
}

void AudioProcessorTest::testRealTimeProcessing()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    // Simulate real-time processing with overlapping chunks
    const int chunkSize = 1024;
    const int totalSamples = 10240;
    QVector<float> fullSignal = generateSineWave(440.0f, 44100.0f, totalSamples);

    for (int i = 0; i < totalSamples - chunkSize; i += chunkSize/2) {
        QVector<float> chunk = fullSignal.mid(i, chunkSize);
        VisualizationData vizData;

        bool success = processor.processAudioData(chunk, format, vizData);
        QVERIFY(success);

        if (!vizData.spectrum.isEmpty()) {
            verifySpectrumPeak(vizData.spectrum, 440.0f, 44100.0f);
        }
    }
}

void AudioProcessorTest::testPerformanceCharacteristics()
{
    AudioConfiguration config = createTestConfiguration();
    AudioProcessor processor(config);
    QAudioFormat format = createStandardFormat();

    QVector<float> samples = generateSineWave(440.0f, 44100.0f, 4096);

    // Measure processing time
    QElapsedTimer timer;
    timer.start();

    const int iterations = 100;
    for (int i = 0; i < iterations; ++i) {
        VisualizationData vizData;
        processor.processAudioData(samples, format, vizData);
    }

    qint64 elapsedMs = timer.elapsed();
    double avgTimeMs = (double)elapsedMs / iterations;

    qDebug() << "Average processing time:" << avgTimeMs << "ms per frame";

    // Performance assertion - should process faster than real-time
    // 4096 samples at 44.1kHz = ~92.88ms of audio
    QVERIFY2(avgTimeMs < 50.0, "Processing too slow for real-time");
}

QTEST_MAIN(AudioProcessorTest)
#include "test_audio_processor.moc"
