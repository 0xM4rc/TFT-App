
include "include/audio_processor.h"
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <cmath>
#include <immintrin.h> // For SIMD operations

// ============================================================================
// LockFreeRingBuffer Implementation (Header-only template)
// ============================================================================

// ============================================================================
// FFTProcessor Implementation
// ============================================================================

FFTProcessor::FFTProcessor(int fftSize, WindowType windowType)
    : m_fftSize(fftSize)
    , m_windowType(windowType)
    , m_windowBuffer(nullptr)
    , m_inputBuffer(nullptr)
    , m_outputBuffer(nullptr)
    , m_plan(nullptr)
{
    if (!initializeFFT()) {
        qCritical() << "Failed to initialize FFT processor";
    }
    generateWindow();
}

FFTProcessor::~FFTProcessor()
{
    cleanupFFT();
}

bool FFTProcessor::setFFTSize(int size)
{
    // Validate FFT size (must be power of 2)
    if (size < 64 || size > MAX_FFT_SIZE || (size & (size - 1)) != 0) {
        qWarning() << "Invalid FFT size:" << size << "(must be power of 2 between 64 and" << MAX_FFT_SIZE << ")";
        return false;
    }

    if (size == m_fftSize) return true;

    QMutexLocker locker(&m_fftMutex);

    // Clean up old resources
    cleanupFFT();

    m_fftSize = size;

    // Reinitialize with new size
    if (!initializeFFT()) {
        qCritical() << "Failed to reinitialize FFT with size" << size;
        return false;
    }

    generateWindow();
    return true;
}

void FFTProcessor::setWindowType(WindowType type)
{
    if (type == m_windowType) return;

    m_windowType = type;
    generateWindow();
}

bool FFTProcessor::processFFT(const float* input, float* magnitudeOutput, float* phaseOutput)
{
    if (!input || !magnitudeOutput || !m_plan) {
        return false;
    }

    QMutexLocker locker(&m_fftMutex);

    // Apply window function
    applyWindow(const_cast<float*>(input), m_windowBuffer, m_fftSize);

    // Copy windowed data to input buffer
    std::memcpy(m_inputBuffer, input, m_fftSize * sizeof(float));

    // Execute FFT
    fftwf_execute(m_plan);

    // Extract magnitude and phase
    const int numBins = m_fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i) {
        const float real = m_outputBuffer[i][0];
        const float imag = m_outputBuffer[i][1];

        magnitudeOutput[i] = std::sqrt(real * real + imag * imag);

        if (phaseOutput) {
            phaseOutput[i] = std::atan2(imag, real);
        }
    }

    return true;
}

void FFTProcessor::convertToDecibels(float* data, int size, float minDb)
{
    // Vectorized conversion to decibels
    for (int i = 0; i < size; ++i) {
        if (data[i] > 0.0f) {
            data[i] = 20.0f * std::log10(data[i]);
            if (data[i] < minDb) {
                data[i] = minDb;
            }
        } else {
            data[i] = minDb;
        }
    }
}

void FFTProcessor::applyWindow(float* data, const float* window, int size)
{
    // SIMD-optimized windowing (if available)
#ifdef __AVX__
    const int simdSize = size & ~7; // Round down to multiple of 8
    for (int i = 0; i < simdSize; i += 8) {
        __m256 dataVec = _mm256_loadu_ps(&data[i]);
        __m256 windowVec = _mm256_loadu_ps(&window[i]);
        __m256 result = _mm256_mul_ps(dataVec, windowVec);
        _mm256_storeu_ps(&data[i], result);
    }
    // Handle remaining elements
    for (int i = simdSize; i < size; ++i) {
        data[i] *= window[i];
    }
#else
    for (int i = 0; i < size; ++i) {
        data[i] *= window[i];
    }
#endif
}

bool FFTProcessor::initializeFFT()
{
    // Allocate aligned memory for SIMD operations
    m_windowBuffer = static_cast<float*>(fftwf_alloc_real(m_fftSize));
    m_inputBuffer = static_cast<float*>(fftwf_alloc_real(m_fftSize));
    m_outputBuffer = static_cast<fftwf_complex*>(fftwf_alloc_complex(m_fftSize / 2 + 1));

    if (!m_windowBuffer || !m_inputBuffer || !m_outputBuffer) {
        cleanupFFT();
        return false;
    }

    // Create FFTW plan
    m_plan = fftwf_plan_dft_r2c_1d(m_fftSize, m_inputBuffer, m_outputBuffer, FFTW_MEASURE);

    if (!m_plan) {
        cleanupFFT();
        return false;
    }

    return true;
}

void FFTProcessor::cleanupFFT()
{
    if (m_plan) {
        fftwf_destroy_plan(m_plan);
        m_plan = nullptr;
    }
    if (m_windowBuffer) {
        fftwf_free(m_windowBuffer);
        m_windowBuffer = nullptr;
    }
    if (m_inputBuffer) {
        fftwf_free(m_inputBuffer);
        m_inputBuffer = nullptr;
    }
    if (m_outputBuffer) {
        fftwf_free(m_outputBuffer);
        m_outputBuffer = nullptr;
    }
}

void FFTProcessor::generateWindow()
{
    if (!m_windowBuffer) return;

    const float N = static_cast<float>(m_fftSize - 1);

    switch (m_windowType) {
    case Hanning:
        for (int i = 0; i < m_fftSize; ++i) {
            m_windowBuffer[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / N));
        }
        break;
    case Hamming:
        for (int i = 0; i < m_fftSize; ++i) {
            m_windowBuffer[i] = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / N);
        }
        break;
    case Blackman:
        for (int i = 0; i < m_fftSize; ++i) {
            const float cos1 = std::cos(2.0f * M_PI * i / N);
            const float cos2 = std::cos(4.0f * M_PI * i / N);
            m_windowBuffer[i] = 0.42f - 0.5f * cos1 + 0.08f * cos2;
        }
        break;
    case Rectangle:
    default:
        std::fill(m_windowBuffer, m_windowBuffer + m_fftSize, 1.0f);
        break;
    }
}

// ============================================================================
// LevelAnalyzer Implementation
// ============================================================================

LevelAnalyzer::LevelAnalyzer(int sampleRate)
    : m_peakDecay(std::exp(-1.0f / (0.3f * sampleRate))) // 300ms decay
    , m_rmsDecay(std::exp(-1.0f / (0.1f * sampleRate)))  // 100ms decay
    , m_vuDecay(std::exp(-1.0f / (0.3f * sampleRate)))   // 300ms decay
    , m_lastUpdate(std::chrono::steady_clock::now())
{
}

void LevelAnalyzer::processSamples(const float* samples, int count)
{
    if (!samples || count <= 0) return;

    float peak = 0.0f;
    float sumSquares = 0.0f;

    // Vectorized level analysis
    for (int i = 0; i < count; ++i) {
        const float sample = std::abs(samples[i]);
        peak = std::max(peak, sample);
        sumSquares += sample * sample;
    }

    const float rms = std::sqrt(sumSquares / count);

    // Apply decay based on time elapsed
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_lastUpdate);
    float decayFactor = std::exp(-elapsed.count() / 1000000.0f); // Convert to seconds
    m_lastUpdate = now;

    // Update levels with appropriate decay
    float currentPeak = m_peakLevel.load();
    m_peakLevel.store(std::max(peak, currentPeak * m_peakDecay * decayFactor));

    float currentRMS = m_rmsLevel.load();
    m_rmsLevel.store(std::max(rms, currentRMS * m_rmsDecay * decayFactor));

    float currentVU = m_vuLevel.load();
    m_vuLevel.store(std::max(rms, currentVU * m_vuDecay * decayFactor));
}

void LevelAnalyzer::reset()
{
    m_peakLevel.store(0.0f);
    m_rmsLevel.store(0.0f);
    m_vuLevel.store(0.0f);
    m_lastUpdate = std::chrono::steady_clock::now();
}

// ============================================================================
// AudioProcessor Implementation
// ============================================================================

AudioProcessor::AudioProcessor(const AudioConfiguration& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_fftProcessor(std::make_unique<FFTProcessor>(config.fftSize,
                                                    static_cast<FFTProcessor::WindowType>(config.windowType)))
    , m_levelAnalyzer(std::make_unique<LevelAnalyzer>(44100)) // Default sample rate
    , m_processingTimer(new QTimer(this))
    , m_processingThread(nullptr)
{
    // Initialize buffers
    m_workBuffer.resize(config.fftSize * 2);
    m_fftInput.resize(config.fftSize);
    m_magnitudeOutput.resize(config.fftSize / 2 + 1);
    m_waveformBuffer.resize(config.waveformBufferSize);

    // Initialize spectrogram ring buffer
    m_spectrogramRingBuffer.resize(config.spectrogramHistory);
    for (auto& row : m_spectrogramRingBuffer) {
        row.resize(config.fftSize / 2 + 1);
    }

    // Initialize smoothing
    m_previousSpectrum.resize(config.fftSize / 2 + 1, 0.0f);

    // Setup processing timer
    m_processingTimer->setInterval(config.updateRateMs);
    connect(m_processingTimer, &QTimer::timeout, this, &AudioProcessor::processAudioBuffer);

    // Initialize performance tracking
    m_lastProcessTime = std::chrono::steady_clock::now();

    qDebug() << "AudioProcessor initialized with FFT size:" << config.fftSize
             << "update rate:" << config.updateRateMs << "ms";
}

AudioProcessor::~AudioProcessor()
{
    stop();
    if (m_processingThread) {
        m_processingThread->quit();
        m_processingThread->wait();
    }
}

bool AudioProcessor::pushAudioData(const float* samples, int count, const QAudioFormat& format)
{
    if (!samples || count <= 0 || !m_isRunning.load()) {
        return false;
    }

    // Update audio format if changed
    const int newSampleRate = format.sampleRate();
    const int newChannels = format.channelCount();

    if (newSampleRate != m_sampleRate.load() || newChannels != m_channels.load()) {
        m_sampleRate.store(newSampleRate);
        m_channels.store(newChannels);
        m_levelAnalyzer = std::make_unique<LevelAnalyzer>(newSampleRate);
    }

    // Push to lock-free buffer
    if (!m_audioBuffer.push(samples, count)) {
        // Buffer overflow - update statistics
        QMutexLocker locker(&m_statsMutex);
        m_stats.bufferUnderruns++;
        return false;
    }

    return true;
}

bool AudioProcessor::getVisualizationData(VisualizationData& vizData)
{
    const int readBuffer = m_currentReadBuffer.load();
    auto& buffer = m_vizBuffers[readBuffer];

    if (!buffer.ready.load()) {
        return false;
    }

    QMutexLocker locker(&buffer.mutex);
    vizData = buffer.data;
    buffer.ready.store(false);

    return true;
}

void AudioProcessor::setConfiguration(const AudioConfiguration& config)
{
    if (!config.isValid()) {
        qWarning() << "Invalid audio configuration provided";
        return;
    }

    m_config = config;

    // Update FFT processor
    m_fftProcessor->setFFTSize(config.fftSize);
    m_fftProcessor->setWindowType(static_cast<FFTProcessor::WindowType>(config.windowType));

    // Update timer
    m_processingTimer->setInterval(config.updateRateMs);

    // Resize buffers
    m_workBuffer.resize(config.fftSize * 2);
    m_fftInput.resize(config.fftSize);
    m_magnitudeOutput.resize(config.fftSize / 2 + 1);
    m_waveformBuffer.resize(config.waveformBufferSize);

    // Resize spectrogram
    m_spectrogramRingBuffer.resize(config.spectrogramHistory);
    for (auto& row : m_spectrogramRingBuffer) {
        row.resize(config.fftSize / 2 + 1);
    }

    m_previousSpectrum.resize(config.fftSize / 2 + 1, 0.0f);

    qDebug() << "AudioProcessor configuration updated";
}

void AudioProcessor::start()
{
    if (m_isRunning.load()) return;

    // Create processing thread if needed
    if (!m_processingThread) {
        m_processingThread = new QThread(this);
        m_processingTimer->moveToThread(m_processingThread);
        m_processingThread->start(QThread::HighPriority);
    }

    m_isRunning.store(true);
    m_processingTimer->start();

    qDebug() << "AudioProcessor started";
}

void AudioProcessor::stop()
{
    if (!m_isRunning.load()) return;

    m_isRunning.store(false);
    m_processingTimer->stop();

    qDebug() << "AudioProcessor stopped";
}

void AudioProcessor::reset()
{
    m_audioBuffer.clear();

    // Reset visualization buffers
    for (auto& buffer : m_vizBuffers) {
        QMutexLocker locker(&buffer.mutex);
        buffer.ready.store(false);
        buffer.data = VisualizationData{};
    }

    // Reset spectrogram
    m_spectrogramWritePos.store(0);
    for (auto& row : m_spectrogramRingBuffer) {
        std::fill(row.begin(), row.end(), 0.0f);
    }

    // Reset smoothing
    std::fill(m_previousSpectrum.begin(), m_previousSpectrum.end(), 0.0f);

    // Reset level analyzer
    m_levelAnalyzer->reset();

    // Reset statistics
    QMutexLocker locker(&m_statsMutex);
    m_stats = PerformanceStats{};
    m_processedFrames = 0;

    qDebug() << "AudioProcessor reset";
}

AudioProcessor::PerformanceStats AudioProcessor::getPerformanceStats() const
{
    QMutexLocker locker(&m_statsMutex);
    return m_stats;
}

void AudioProcessor::processAudioBuffer()
{
    if (!m_isRunning.load()) return;

    auto startTime = std::chrono::steady_clock::now();

    // Get available data
    const size_t available = m_audioBuffer.available();
    if (available < static_cast<size_t>(m_config.fftSize)) {
        return; // Not enough data
    }

    // Pop data from buffer
    const size_t toPop = std::min(available, m_workBuffer.size());
    const size_t actuallyPopped = m_audioBuffer.pop(m_workBuffer.data(), toPop);

    if (actuallyPopped == 0) return;

    // Get current write buffer
    const int writeBuffer = m_currentWriteBuffer.load();
    auto& buffer = m_vizBuffers[writeBuffer];

    QMutexLocker locker(&buffer.mutex);

    // Process FFT if we have enough data
    if (processFFTData(m_workBuffer.data(), actuallyPopped)) {
        // Update spectrogram
        updateSpectrogram(m_magnitudeOutput);

        // Fill visualization data
        buffer.data.spectrum = QVector<float>(m_magnitudeOutput.begin(), m_magnitudeOutput.end());
        buffer.data.fftSize = m_config.fftSize;
        buffer.data.frequencyResolution = static_cast<float>(m_sampleRate.load()) / m_config.fftSize;
    }

    // Update waveform
    updateWaveform(m_workBuffer.data(), actuallyPopped);
    downsampleWaveform(m_waveformBuffer.data(), m_waveformBuffer.size(),
                       buffer.data.waveform.data(), 512);
    buffer.data.waveform.resize(512);

    // Process levels
    processLevelsVector(m_workBuffer.data(), actuallyPopped);
    buffer.data.peakLevel = m_levelAnalyzer->getPeakLevel();
    buffer.data.rmsLevel = m_levelAnalyzer->getRMSLevel();

    // Fill metadata
    buffer.data.sampleRate = m_sampleRate.load();
    buffer.data.channels = m_channels.load();
    buffer.data.timestamp = QDateTime::currentMSecsSinceEpoch();

    // Copy spectrogram
    buffer.data.spectrogram.clear();
    buffer.data.spectrogram.reserve(m_config.spectrogramHistory);
    for (const auto& row : m_spectrogramRingBuffer) {
        buffer.data.spectrogram.append(QVector<float>(row.begin(), row.end()));
    }

    // Mark buffer ready and switch
    buffer.ready.store(true);
    switchVisualizationBuffers();

    // Update performance statistics
    updatePerformanceStats();

    // Emit signal
    emit dataReady(buffer.data);

    auto endTime = std::chrono::steady_clock::now();
    auto processingTime = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

    // Check for performance issues
    const float processingTimeMs = processingTime.count() / 1000.0f;
    const float maxAllowedMs = m_config.updateRateMs * 0.8f; // 80% of update period

    if (processingTimeMs > maxAllowedMs) {
        emit performanceWarning(QString("High processing load: %1ms (limit: %2ms)")
                                    .arg(processingTimeMs, 0, 'f', 2)
                                    .arg(maxAllowedMs, 0, 'f', 2));
    }
}

bool AudioProcessor::processFFTData(const float* samples, int count)
{
    if (count < m_config.fftSize) return false;

    // Take the last FFT size samples
    const int offset = count - m_config.fftSize;
    std::copy(samples + offset, samples + offset + m_config.fftSize, m_fftInput.begin());

    // Process FFT
    if (!m_fftProcessor->processFFT(m_fftInput.data(), m_magnitudeOutput.data())) {
        return false;
    }

    // Convert to decibels
    FFTProcessor::convertToDecibels(m_magnitudeOutput.data(), m_magnitudeOutput.size(), m_config.minDecibels);

    // Apply smoothing
    smoothSpectrum(m_magnitudeOutput);

    return true;
}

void AudioProcessor::updateSpectrogram(const std::vector<float>& spectrum)
{
    const int writePos = m_spectrogramWritePos.load();

    // Copy spectrum to ring buffer
    std::copy(spectrum.begin(), spectrum.end(), m_spectrogramRingBuffer[writePos].begin());

    // Update write position
    m_spectrogramWritePos.store((writePos + 1) % m_config.spectrogramHistory);
}

void AudioProcessor::updateWaveform(const float* samples, int count)
{
    // Simple circular buffer update
    for (int i = 0; i < count && i < static_cast<int>(m_waveformBuffer.size()); ++i) {
        m_waveformBuffer[i] = samples[i];
    }
}

void AudioProcessor::smoothSpectrum(std::vector<float>& spectrum)
{
    if (spectrum.size() != m_previousSpectrum.size()) {
        m_previousSpectrum = spectrum;
        return;
    }

    for (size_t i = 0; i < spectrum.size(); ++i) {
        spectrum[i] = m_smoothingFactor * m_previousSpectrum[i] +
                      (1.0f - m_smoothingFactor) * spectrum[i];
    }

    m_previousSpectrum = spectrum;
}

void AudioProcessor::updatePerformanceStats()
{
    QMutexLocker locker(&m_statsMutex);

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastProcessTime);

    m_processedFrames++;

    if (elapsed.count() >= 1000) { // Update every second
        m_stats.processingLoadPercent = 50.0f; // Placeholder calculation
        m_stats.averageLatencyMs = m_config.updateRateMs; // Placeholder

        m_lastProcessTime = now;
        m_processedFrames = 0;
    }
}

void AudioProcessor::switchVisualizationBuffers()
{
    const int currentWrite = m_currentWriteBuffer.load();
    const int newWrite = (currentWrite + 1) % 2;
    const int newRead = currentWrite;

    m_currentWriteBuffer.store(newWrite);
    m_currentReadBuffer.store(newRead);
}

void AudioProcessor::processLevelsVector(const float* samples, int count)
{
    m_levelAnalyzer->processSamples(samples, count);
}

void AudioProcessor::downsampleWaveform(const float* input, int inputSize, float* output, int outputSize)
{
    if (inputSize <= outputSize) {
        std::copy(input, input + inputSize, output);
        return;
    }

    const float ratio = static_cast<float>(inputSize) / outputSize;

    for (int i = 0; i < outputSize; ++i) {
        const int sourceIndex = static_cast<int>(i * ratio);
        output[i] = input[std::min(sourceIndex, inputSize - 1)];
    }
}
