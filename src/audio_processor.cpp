#include "include/audio_processor.h"
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <fftw3.h>

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
    , m_inputRealBuffer(nullptr)
    , m_fftBuffer(nullptr)
    , m_plan(nullptr)
{
    // 1) Crear ventana por defecto
    generateWindow();
    // 2) Inicializar FFT con el tamaño indicado
    initializeFFT();
}


FFTProcessor::~FFTProcessor()
{
    cleanupFFT();
}

void FFTProcessor::setFFTSize(int size)
{
    // Sólo potencias de 2 razonables
    if (size < 64 || size > 8192 || (size & (size - 1)) != 0) {
        qWarning() << "FFT size must be power of 2 between 64 and 8192";
        return;
    }
    // Si no cambia, no hacemos nada
    if (size == m_fftSize) return;

    // 1) Liberar plan/buffers viejos
    cleanupFFT();

    // 2) Guardar nuevo tamaño
    m_fftSize = size;

    // 3) Regenerar la ventana con el nuevo tamaño
    generateWindow();

    // 4) (Re)inicializar FFT: reserva buffers y crea el plan
    initializeFFT();

    qDebug() << "[FFT] setFFTSize =>" << m_fftSize
             << "window and FFT reinitialized.";
}


void FFTProcessor::setWindowType(WindowType type)
{
    if (type == m_windowType) return;

    m_windowType = type;
    generateWindow();
}

void FFTProcessor::initializeFFT()
{
    // Primero, limpia cualquier estado previo
    cleanupFFT();

    // Reserva el buffer real de entrada
    m_inputRealBuffer = (float*)fftwf_alloc_real(m_fftSize);
    Q_ASSERT(m_inputRealBuffer);

    // Reserva el buffer complejo de salida (N/2+1 celdas)
    int complexSize = m_fftSize/2 + 1;
    m_fftBuffer = (fftwf_complex*)fftwf_alloc_complex(complexSize);
    Q_ASSERT(m_fftBuffer);

    // Crea el plan real→complex
    m_plan = fftwf_plan_dft_r2c_1d(
        m_fftSize,
        m_inputRealBuffer,
        m_fftBuffer,
        FFTW_MEASURE
        );
    Q_ASSERT(m_plan);
}



void FFTProcessor::cleanupFFT()
{
    if (m_plan) {
        fftwf_destroy_plan(m_plan);
        m_plan = nullptr;
    }
    if (m_inputRealBuffer) {
        fftwf_free(m_inputRealBuffer);
        m_inputRealBuffer = nullptr;
    }
    if (m_fftBuffer) {
        fftwf_free(m_fftBuffer);
        m_fftBuffer = nullptr;
    }
}


void FFTProcessor::generateWindow()
{
    m_window.resize(m_fftSize);
    switch (m_windowType) {
    case Hanning:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.5f * (1 - qCos(2 * M_PI * i / (m_fftSize - 1)));
        }
        break;
    case Hamming:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.54f - 0.46f * qCos(2 * M_PI * i / (m_fftSize - 1));
        }
        break;
    case Blackman:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 0.42f - 0.5f * qCos(2 * M_PI * i / (m_fftSize - 1))
            + 0.08f * qCos(4 * M_PI * i / (m_fftSize - 1));
        }
        break;
    case Rectangle:
    default:
        for (int i = 0; i < m_fftSize; ++i) {
            m_window[i] = 1.0f;
        }
        break;
    }
}


bool FFTProcessor::processFFT(const QVector<float>& input, QVector<float>& output)
{
    // 1) Precondiciones
    Q_ASSERT(input.size() == m_fftSize);
    Q_ASSERT(m_plan);
    Q_ASSERT(m_inputRealBuffer);
    Q_ASSERT(m_fftBuffer);

    if (input.size() != m_fftSize || !m_plan) {
        qWarning() << "[FFT] Bad preconditions: input.size() =" << input.size()
        << "m_fftSize=" << m_fftSize
        << "plan?" << (m_plan != nullptr);
        return false;
    }

    // 2) Copiar + ventana
    for (int i = 0; i < m_fftSize; ++i) {
        Q_ASSERT(i < m_window.size());
        m_inputRealBuffer[i] = input[i] * m_window[i];
    }
    qDebug() << "[FFT] Copied inputRealBuffer[0..2] ="
             << m_inputRealBuffer[0] << m_inputRealBuffer[1] << m_inputRealBuffer[2];

    // 3) Ejecutar FFTW
    fftwf_execute(m_plan);
    qDebug() << "[FFT] fftwf_execute completed";

    // 4) Extraer magnitudes
    int outSize = m_fftSize/2 + 1;
    output.resize(outSize);

    // **Verifica que m_fftBuffer tenga al menos outSize filas y 2 columnas**
    for (int i = 0; i < outSize; ++i) {
        // Si esto falla, buffer mal dimensionado
        Q_ASSERT(i < m_fftSize);
        // Suponemos que m_fftBuffer[i] es un puntero a dos floats
        float re = m_fftBuffer[i][0];
        float im = m_fftBuffer[i][1];
        if (!std::isfinite(re) || !std::isfinite(im)) {
            qWarning() << "[FFT] Non-finite FFT result at bin" << i << "re=" << re << "im=" << im;
        }
        output[i] = qSqrt(re*re + im*im);
    }
    qDebug() << "[FFT] output[0..2] =" << output.mid(0,3);

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

// ============================================================================
// AudioProcessor Implementation
// ============================================================================

AudioProcessor::AudioProcessor(const AudioConfiguration& config,
                               QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_isInitialized(false)
    , m_enabled(true)
    , m_fftSize(config.fftSize)
    , m_overlap(config.overlap)
    , m_spectrogramHistory(config.spectrogramHistory)
    , m_waveformBufferSize(config.waveformBufferSize)
    , m_minFrequency(config.minFrequency)
    , m_maxFrequency(config.maxFrequency)
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

// AudioProcessor::~AudioProcessor()
// {
//     qDebug() << "AudioProcessor destroyed";
// }

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
    qDebug() << "[AP] ENTER processAudioData:"
             << "samples.size() =" << samples.size()
             << "rate=" << format.sampleRate()
             << "ch="   << format.channelCount()
             << "fftSize=" << m_fftSize
             << "overlap=" << m_overlap;

    // Precondiciones
    Q_ASSERT(m_enabled);
    Q_ASSERT(!samples.isEmpty());
    Q_ASSERT(format.isValid());

    if (!m_enabled) {
        qWarning() << "[AP] disabled, exiting";
        return false;
    }
    if (samples.isEmpty()) {
        qWarning() << "[AP] no samples, exiting";
        return false;
    }

    QMutexLocker locker(&m_processingMutex);

    try {
        // 1) Inicialización
        {
            QElapsedTimer t; t.start();
            ensureInitialized(format);
            qDebug() << "[AP] ensureInitialized done in" << t.nsecsElapsed()/1000 << "µs";
        }

        // 2) Acumular waveform
        {
            QElapsedTimer t; t.start();
            m_waveformBuffer->append(samples);
            qDebug() << "[AP] append to waveformBuffer in" << t.nsecsElapsed()/1000 << "µs"
                     << "waveformBuffer.size() =" << m_waveformBuffer->size();
        }

        // 3) Procesar forma de onda
        {
            QElapsedTimer t; t.start();
            processWaveform(vizData);
            qDebug() << "[AP] processWaveform done in" << t.nsecsElapsed()/1000 << "µs"
                     << "vizData.waveform.size() =" << vizData.waveform.size();
        }

        // 4) Acumular FFT buffer
        {
            QElapsedTimer t; t.start();
            m_processingBuffer.append(samples);
            qDebug() << "[AP] append to processingBuffer in" << t.nsecsElapsed()/1000 << "µs"
                     << "processingBuffer.size() =" << m_processingBuffer.size();
        }

        // 5) Procesar espectro
        if (m_processingBuffer.size() >= m_fftSize) {
            QElapsedTimer t; t.start();
            processSpectrum(m_processingBuffer, vizData);
            qDebug() << "[AP] processSpectrum done in" << t.nsecsElapsed()/1000 << "µs"
                     << "vizData.spectrum.size() =" << vizData.spectrum.size();

            // Q_ASSERT para evitar índice fuera de rango
            int samplesToRemove = m_fftSize - m_overlap;
            Q_ASSERT(samplesToRemove > 0);
            if (samplesToRemove > 0 && samplesToRemove < m_processingBuffer.size()) {
                m_processingBuffer.remove(0, samplesToRemove);
                qDebug() << "[AP] removed" << samplesToRemove
                         << "samples, new processingBuffer.size() ="
                         << m_processingBuffer.size();
            }
        } else {
            qDebug() << "[AP] not enough data for FFT,"
                     << "need" << m_fftSize << "have" << m_processingBuffer.size();
        }

        // 6) Rellenar vizData metadatos
        vizData.sampleRate = format.sampleRate();
        vizData.channels   = format.channelCount();
        vizData.timestamp  = QDateTime::currentMSecsSinceEpoch();
        vizData.spectrogram = m_spectrogramData;

        // 7) Estadísticas internas
        m_samplesProcessed += samples.size();
        qDebug() << "[AP] total samples processed =" << m_samplesProcessed;

        qDebug() << "[AP] EXIT processAudioData OK";
        return true;

    } catch (const std::exception& e) {
        qCritical() << "[AP] Exception in processAudioData:" << e.what();
        emit processingError(QString("Processing error: %1").arg(e.what()));
        return false;
    } catch (...) {
        qCritical() << "[AP] Unknown exception in processAudioData";
        emit processingError("Unknown processing error");
        return false;
    }
}

void AudioProcessor::processWaveform(VisualizationData& vizData)
{
    // 1) Tomar los últimos samples
    int waveformSamples = qMin(2048, m_waveformBufferSize);
    QVector<float> recent = m_waveformBuffer->getLastSamples(waveformSamples);
    if (recent.isEmpty()) {
        vizData.waveform.clear();
        return;
    }

    // 2) (Opcional) Normalizar a [-1,1] por si hay outliers
    float maxAbs = 0.0f;
    for (float v : recent) maxAbs = qMax(maxAbs, qAbs(v));
    if (maxAbs > 1.0f) {
        for (float &v : recent) v /= maxAbs;
    }

    // 3) Remuestrear a un tamaño fijo (512) para la UI
    const int targetSize = 512;
    if (recent.size() > targetSize) {
        vizData.waveform = resampleForVisualization(recent, targetSize);
    } else {
        vizData.waveform = recent;
    }
}


void AudioProcessor::processSpectrum(const QVector<float>& samples, VisualizationData& vizData)
{
    qDebug() << "[AP::processSpectrum] ENTER. samples.size() =" << samples.size()
    << "fftSize=" << m_fftSize;

    // 1) Comprobar tamaño
    if (samples.size() < m_fftSize) {
        qWarning() << "[AP::processSpectrum] Not enough samples for FFT";
        return;
    }

    // 2) Preparar input FFT
    QVector<float> fftInput(m_fftSize);
    int startPos = samples.size() - m_fftSize;
    Q_ASSERT(startPos >= 0);
    for (int i = 0; i < m_fftSize; ++i) {
        fftInput[i] = samples.at(startPos + i);
    }
    qDebug() << "[AP::processSpectrum] fftInput prepared. first =" << fftInput.first()
             << "last =" << fftInput.last();

    // 3) Ejecutar FFT
    QVector<float> spectrum;
    bool fftOk = false;
    try {
        qDebug() << "[AP::processSpectrum] Calling m_fftProcessor->processFFT()";
        Q_ASSERT(m_fftProcessor != nullptr);
        if (!m_fftProcessor) {
            qCritical() << "[AP::processSpectrum] ERROR: m_fftProcessor is null!";
            return;
        }
        fftOk = m_fftProcessor->processFFT(fftInput, spectrum);
        qDebug() << "[AP::processSpectrum] FFT returned" << fftOk
                 << "spectrum.size() =" << spectrum.size();
    } catch (const std::exception& e) {
        qCritical() << "[AP::processSpectrum] Exception in processFFT:" << e.what();
        return;
    } catch (...) {
        qCritical() << "[AP::processSpectrum] Unknown exception in processFFT";
        return;
    }
    if (!fftOk) {
        qWarning() << "[AP::processSpectrum] FFT processing failed";
        return;
    }

    // 4) Suavizar
    try {
        qDebug() << "[AP::processSpectrum] Calling smoothSpectrum()";
        smoothSpectrum(spectrum);
        qDebug() << "[AP::processSpectrum] After smoothSpectrum, spectrum[0..2] ="
                 << spectrum.mid(0,3);
    } catch (const std::exception& e) {
        qCritical() << "[AP::processSpectrum] Exception in smoothSpectrum:" << e.what();
        return;
    } catch (...) {
        qCritical() << "[AP::processSpectrum] Unknown exception in smoothSpectrum";
        return;
    }

    // 5) Decibelios
    try {
        qDebug() << "[AP::processSpectrum] Converting to decibels";
        FFTProcessor::convertToDecibels(spectrum, -80.0f);
        qDebug() << "[AP::processSpectrum] After convertToDecibels, spectrum[0..2] ="
                 << spectrum.mid(0,3);
    } catch (const std::exception& e) {
        qCritical() << "[AP::processSpectrum] Exception in convertToDecibels:" << e.what();
        return;
    } catch (...) {
        qCritical() << "[AP::processSpectrum] Unknown exception in convertToDecibels";
        return;
    }

    // 6) Asignar al vizData
    vizData.spectrum = spectrum;

    // 7) Actualizar espectrograma
    try {
        qDebug() << "[AP::processSpectrum] Updating spectrogram";
        updateSpectrogram(spectrum);
        qDebug() << "[AP::processSpectrum] Spectrogram updated. spectrogramData rows="
                 << m_spectrogramData.size();
    } catch (const std::exception& e) {
        qCritical() << "[AP::processSpectrum] Exception in updateSpectrogram:" << e.what();
        return;
    } catch (...) {
        qCritical() << "[AP::processSpectrum] Unknown exception in updateSpectrogram";
        return;
    }

    // 8) Guardar para siguiente suavizado
    m_previousSpectrum = spectrum;

    qDebug() << "[AP::processSpectrum] EXIT OK";
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

void AudioProcessor::applyConfiguration(const AudioConfiguration& config)
{
    QMutexLocker locker(&m_processingMutex);

    // Guardar nueva configuración
    m_config = config;

    // FFT y solapamiento
    setFFTSize(config.fftSize);
    setOverlap(config.overlap);

    // Historial de espectrograma
    setSpectrogramHistory(config.spectrogramHistory);

    // Buffer de waveform
    setWaveformBufferSize(config.waveformBufferSize);

    // Tipo de ventana
    setWindowType(static_cast<FFTProcessor::WindowType>(config.windowType));

    // Rango de frecuencias
    m_minFrequency = config.minFrequency;
    m_maxFrequency = config.maxFrequency;

    qDebug() << "AudioProcessor configuration applied:"
             << "FFT size =" << m_fftSize
             << "overlap =" << m_overlap
             << "spectrogramHistory =" << m_spectrogramHistory
             << "waveformBufferSize =" << m_waveformBufferSize
             << "frequencyRange =" << m_minFrequency << "-" << m_maxFrequency
             << "windowType =" << config.windowType;
}
