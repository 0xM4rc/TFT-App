#include "include/audio_processor.h"
#include <QDebug>
#include <QDateTime>
#include <algorithm>

const float AudioProcessor::DEFAULT_MIN_FREQUENCY = 20.0f;
const float AudioProcessor::DEFAULT_MAX_FREQUENCY = 20000.0f;

// ============================================================================
// AudioBuffer Implementation
// ============================================================================

AudioBuffer::AudioBuffer(int maxSize)
    : m_writePos(0)
    , m_currentSize(0)
    , m_maxSize(maxSize)
{
    m_buffer.resize(maxSize);
    m_buffer.fill(0.0f);
}

void AudioBuffer::setMaxSize(int size)
{
    QMutexLocker locker(&m_mutex);

    if (size == m_maxSize) return;

    m_buffer.resize(size);
    m_maxSize = size;

    if (m_currentSize > size) {
        m_currentSize = size;
    }
    if (m_writePos >= size) {
        m_writePos = 0;
    }
}

void AudioBuffer::append(const QVector<float>& data)
{
    if (data.isEmpty()) return;
    append(data.constData(), data.size());
}

void AudioBuffer::append(const float* data, int count)
{
    if (!data || count <= 0) return;

    QMutexLocker locker(&m_mutex);

    for (int i = 0; i < count; ++i) {
        m_buffer[m_writePos] = data[i];
        m_writePos = (m_writePos + 1) % m_maxSize;

        if (m_currentSize < m_maxSize) {
            m_currentSize++;
        }
    }
}

QVector<float> AudioBuffer::getLastSamples(int count) const
{
    QMutexLocker locker(&m_mutex);

    if (count <= 0 || m_currentSize == 0) {
        return QVector<float>();
    }

    count = qMin(count, m_currentSize);
    QVector<float> result(count);

    int readPos = (m_writePos - count + m_maxSize) % m_maxSize;

    for (int i = 0; i < count; ++i) {
        result[i] = m_buffer[readPos];
        readPos = (readPos + 1) % m_maxSize;
    }

    return result;
}

QVector<float> AudioBuffer::getAllSamples() const
{
    return getLastSamples(m_currentSize);
}

void AudioBuffer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_buffer.fill(0.0f);
    m_writePos = 0;
    m_currentSize = 0;
}

// ============================================================================
// FFTProcessor Implementation
// ============================================================================

FFTProcessor::FFTProcessor(int fftSize)
    : m_fftSize(fftSize)
    , m_windowType(Hanning)
    , m_fftBuffer(nullptr)
{
    setFFTSize(fftSize);
}

FFTProcessor::~FFTProcessor()
{
    cleanupFFT();
}

void FFTProcessor::setFFTSize(int size)
{
    // Verificar que sea potencia de 2
    if (size < 64 || size > 8192 || (size & (size - 1)) != 0) {
        qWarning() << "FFT size must be power of 2 between 64 and 8192";
        return;
    }

    if (size == m_fftSize) return;

    cleanupFFT();
    m_fftSize = size;
    initializeFFT();
}

void FFTProcessor::setWindowType(WindowType type)
{
    if (type == m_windowType) return;

    m_windowType = type;
    generateWindow();
}

void FFTProcessor::initializeFFT()
{
    m_fftBuffer = new std::complex<float>[m_fftSize];
    m_workBuffer.resize(m_fftSize);
    generateWindow();
}

void FFTProcessor::cleanupFFT()
{
    delete[] m_fftBuffer;
    m_fftBuffer = nullptr;
}

void FFTProcessor::generateWindow()
{
    m_window.resize(m_fftSize);

    switch (m_windowType) {
    case Rectangle:
        m_window.fill(1.0f);
        break;

    case Hanning:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.5f * (1.0f - qCos(2.0f * M_PI * i / (m_fftSize - 1)));
        }
        break;

    case Hamming:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.54f - 0.46f * qCos(2.0f * M_PI * i / (m_fftSize - 1));
        }
        break;

    case Blackman:
        for (int i = 0; i < m_fftSize; ++i) {
            float factor = 2.0f * M_PI * i / (m_fftSize - 1);
            m_window[i] = 0.42f - 0.5f * qCos(factor) + 0.08f * qCos(2.0f * factor);
        }
        break;
    }
}

bool FFTProcessor::processFFT(const QVector<float>& input, QVector<float>& output)
{
    if (input.size() != m_fftSize || !m_fftBuffer) {
        return false;
    }

    // Copiar datos de entrada y aplicar ventana
    for (int i = 0; i < m_fftSize; ++i) {
        m_fftBuffer[i] = std::complex<float>(input[i] * m_window[i], 0.0f);
    }

    // Realizar FFT
    fft(m_fftBuffer, m_fftSize);

    // Calcular magnitudes (solo mitad positiva del espectro)
    int outputSize = m_fftSize / 2;
    output.resize(outputSize);

    for (int i = 0; i < outputSize; ++i) {
        float real = m_fftBuffer[i].real();
        float imag = m_fftBuffer[i].imag();
        output[i] = qSqrt(real * real + imag * imag);
    }

    return true;
}

void FFTProcessor::applyWindow(QVector<float>& data)
{
    if (data.size() != m_fftSize || m_window.size() != m_fftSize) {
        return;
    }

    for (int i = 0; i < m_fftSize; ++i) {
        data[i] *= m_window[i];
    }
}

void FFTProcessor::convertToDecibels(QVector<float>& data, float minDb)
{
    for (float& value : data) {
        if (value > 0.0f) {
            value = 20.0f * log10f(value);
            if (value < minDb) {
                value = minDb;
            }
        } else {
            value = minDb;
        }
    }
}

// Implementación simple de FFT Cooley-Tukey
void FFTProcessor::fft(std::complex<float>* data, int n, bool inverse)
{
    // Bit reversal
    bitReverse(data, n);

    // FFT computation
    for (int len = 2; len <= n; len <<= 1) {
        float angle = (inverse ? 2.0f : -2.0f) * M_PI / len;
        std::complex<float> wlen(cos(angle), sin(angle));

        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (int i = 0; i < n; ++i) {
            data[i] /= n;
        }
    }
}

void FFTProcessor::bitReverse(std::complex<float>* data, int n)
{
    int j = 0;
    for (int i = 1; i < n; ++i) {
        int bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;

        if (i < j) {
            std::swap(data[i], data[j]);
        }
    }
}

// ============================================================================
// AudioProcessor Implementation
// ============================================================================

AudioProcessor::AudioProcessor(QObject* parent)
    : QObject(parent)
    , m_isInitialized(false)
    , m_enabled(true)
    , m_fftSize(DEFAULT_FFT_SIZE)
    , m_overlap(DEFAULT_OVERLAP)
    , m_spectrogramHistory(DEFAULT_SPECTROGRAM_HISTORY)
    , m_waveformBufferSize(DEFAULT_WAVEFORM_BUFFER_SIZE)
    , m_minFrequency(DEFAULT_MIN_FREQUENCY)
    , m_maxFrequency(DEFAULT_MAX_FREQUENCY)
    , m_samplesProcessed(0)
{
    // Inicializar componentes
    m_waveformBuffer = std::make_unique<AudioBuffer>(m_waveformBufferSize);
    m_fftProcessor = std::make_unique<FFTProcessor>(m_fftSize);

    // Conectar señales de error
    connect(this, &AudioProcessor::processingError,
            this, &AudioProcessor::handleProcessingError);

    qDebug() << "AudioProcessor initialized with FFT size:" << m_fftSize;
}

AudioProcessor::~AudioProcessor()
{
    qDebug() << "AudioProcessor destroyed";
}

void AudioProcessor::setFFTSize(int size)
{
    QMutexLocker locker(&m_processingMutex);

    if (size == m_fftSize) return;

    m_fftSize = size;
    m_overlap = size / 2; // 50% overlap por defecto

    if (m_fftProcessor) {
        m_fftProcessor->setFFTSize(size);
    }

    // Redimensionar buffer de procesamiento
    m_processingBuffer.clear();
    m_processingBuffer.reserve(size * 2);

    qDebug() << "FFT size set to:" << m_fftSize << "overlap:" << m_overlap;
}

int AudioProcessor::getFFTSize() const
{
    QMutexLocker locker(&m_processingMutex);
    return m_fftSize;
}

void AudioProcessor::setOverlap(int samples)
{
    QMutexLocker locker(&m_processingMutex);

    if (samples >= 0 && samples < m_fftSize) {
        m_overlap = samples;
        qDebug() << "Overlap set to:" << m_overlap << "samples";
    }
}

void AudioProcessor::setSpectrogramHistory(int frames)
{
    QMutexLocker locker(&m_processingMutex);

    if (frames > 0 && frames <= 1000) {
        m_spectrogramHistory = frames;

        // Redimensionar datos del espectrograma
        while (m_spectrogramData.size() > frames) {
            m_spectrogramData.removeFirst();
        }

        qDebug() << "Spectrogram history set to:" << m_spectrogramHistory << "frames";
    }
}

void AudioProcessor::setWindowType(FFTProcessor::WindowType type)
{
    QMutexLocker locker(&m_processingMutex);

    if (m_fftProcessor) {
        m_fftProcessor->setWindowType(type);
        qDebug() << "Window type set to:" << type;
    }
}

FFTProcessor::WindowType AudioProcessor::getWindowType() const
{
    QMutexLocker locker(&m_processingMutex);
    return m_fftProcessor ? m_fftProcessor->getWindowType() : FFTProcessor::Hanning;
}

void AudioProcessor::setWaveformBufferSize(int samples)
{
    QMutexLocker locker(&m_processingMutex);

    if (samples > 0 && samples <= 32768) {
        m_waveformBufferSize = samples;
        if (m_waveformBuffer) {
            m_waveformBuffer->setMaxSize(samples);
        }
        qDebug() << "Waveform buffer size set to:" << m_waveformBufferSize;
    }
}

int AudioProcessor::getWaveformBufferSize() const
{
    QMutexLocker locker(&m_processingMutex);
    return m_waveformBufferSize;
}

void AudioProcessor::setFrequencyRange(float minFreq, float maxFreq)
{
    QMutexLocker locker(&m_processingMutex);

    if (minFreq >= 0 && maxFreq > minFreq && maxFreq <= 50000) {
        m_minFrequency = minFreq;
        m_maxFrequency = maxFreq;
        qDebug() << "Frequency range set to:" << minFreq << "-" << maxFreq << "Hz";
    }
}

void AudioProcessor::getFrequencyRange(float& minFreq, float& maxFreq) const
{
    QMutexLocker locker(&m_processingMutex);
    minFreq = m_minFrequency;
    maxFreq = m_maxFrequency;
}

bool AudioProcessor::processAudioData(const QVector<float>& samples,
                                      const QAudioFormat& format,
                                      VisualizationData& vizData)
{
    if (!m_enabled || samples.isEmpty()) {
        return false;
    }

    QMutexLocker locker(&m_processingMutex);

    try {
        // Asegurar inicialización
        ensureInitialized(format);

        // Agregar muestras al buffer de forma de onda
        m_waveformBuffer->append(samples);

        // Procesar forma de onda
        processWaveform(vizData);

        // Agregar al buffer de procesamiento FFT
        m_processingBuffer.append(samples);

        // Procesar espectro si tenemos suficientes datos
        if (m_processingBuffer.size() >= m_fftSize) {
            processSpectrum(m_processingBuffer, vizData);

            // Remover datos procesados (con overlap)
            int samplesToRemove = m_fftSize - m_overlap;
            if (samplesToRemove > 0 && samplesToRemove < m_processingBuffer.size()) {
                m_processingBuffer.remove(0, samplesToRemove);
            }
        }

        // Actualizar información del formato
        vizData.sampleRate = format.sampleRate();
        vizData.channels = format.channelCount();
        vizData.timestamp = QDateTime::currentMSecsSinceEpoch();

        // Copiar datos del espectrograma
        vizData.spectrogram = m_spectrogramData;

        m_samplesProcessed += samples.size();

        return true;

    } catch (const std::exception& e) {
        emit processingError(QString("Processing error: %1").arg(e.what()));
        return false;
    } catch (...) {
        emit processingError("Unknown processing error");
        return false;
    }
}

void AudioProcessor::processWaveform(VisualizationData& vizData)
{
    // Obtener datos recientes para visualización
    int waveformSamples = qMin(2048, m_waveformBufferSize); // Máximo 2048 puntos para visualización
    QVector<float> recentSamples = m_waveformBuffer->getLastSamples(waveformSamples);

    if (recentSamples.isEmpty()) {
        vizData.waveform.clear();
        return;
    }

    // Remuestrear para visualización si es necesario
    const int targetWaveformSize = 512; // Tamaño fijo para visualización
    if (recentSamples.size() > targetWaveformSize) {
        vizData.waveform = resampleForVisualization(recentSamples, targetWaveformSize);
    } else {
        vizData.waveform = recentSamples;
    }
}

void AudioProcessor::processSpectrum(const QVector<float>& samples, VisualizationData& vizData)
{
    if (samples.size() < m_fftSize) {
        return;
    }

    // Tomar las últimas muestras para FFT
    QVector<float> fftInput(m_fftSize);
    int startPos = samples.size() - m_fftSize;
    for (int i = 0; i < m_fftSize; ++i) {
        fftInput[i] = samples[startPos + i];
    }

    // Procesar FFT
    QVector<float> spectrum;
    if (m_fftProcessor->processFFT(fftInput, spectrum)) {
        // Suavizar el espectro
        smoothSpectrum(spectrum);

        // Convertir a decibelios
        FFTProcessor::convertToDecibels(spectrum, -80.0f);

        vizData.spectrum = spectrum;

        // Actualizar espectrograma
        updateSpectrogram(spectrum);

        // Guardar espectro para próximo suavizado
        m_previousSpectrum = spectrum;
    }
}

void AudioProcessor::updateSpectrogram(const QVector<float>& spectrum)
{
    if (spectrum.isEmpty()) return;

    // Agregar nuevo frame al espectrograma
    m_spectrogramData.append(spectrum);

    // Mantener solo el historial configurado
    while (m_spectrogramData.size() > m_spectrogramHistory) {
        m_spectrogramData.removeFirst();
    }
}

void AudioProcessor::smoothSpectrum(QVector<float>& spectrum, float smoothingFactor)
{
    if (m_previousSpectrum.size() != spectrum.size()) {
        m_previousSpectrum = spectrum;
        return;
    }

    for (int i = 0; i < spectrum.size(); ++i) {
        spectrum[i] = smoothingFactor * m_previousSpectrum[i] + (1.0f - smoothingFactor) * spectrum[i];
    }
}

QVector<float> AudioProcessor::resampleForVisualization(const QVector<float>& input, int targetSize)
{
    if (input.size() <= targetSize) {
        return input;
    }

    QVector<float> output(targetSize);
    float ratio = (float)input.size() / targetSize;

    for (int i = 0; i < targetSize; ++i) {
        int sourceIndex = (int)(i * ratio);
        if (sourceIndex < input.size()) {
            output[i] = input[sourceIndex];
        }
    }

    return output;
}

void AudioProcessor::ensureInitialized(const QAudioFormat& format)
{
    if (m_isInitialized && m_currentFormat == format) {
        return;
    }

    m_currentFormat = format;
    m_isInitialized = true;

    qDebug() << "AudioProcessor initialized for format:"
             << format.sampleRate() << "Hz"
             << format.channelCount() << "ch"
             << format.sampleFormat();
}

void AudioProcessor::reset()
{
    QMutexLocker locker(&m_processingMutex);

    if (m_waveformBuffer) {
        m_waveformBuffer->clear();
    }

    m_processingBuffer.clear();
    m_samplesProcessed = 0;

    clearHistory();

    qDebug() << "AudioProcessor reset";
}

void AudioProcessor::clearHistory()
{
    m_spectrogramData.clear();
    m_previousSpectrum.clear();

    qDebug() << "AudioProcessor history cleared";
}

void AudioProcessor::handleProcessingError(const QString& error)
{
    qWarning() << "AudioProcessor error:" << error;
}
