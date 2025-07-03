#include "realtime_spectrogram.h"
#include <QDebug>
#include <QDateTime>
#include <QtMath>
#include <algorithm>

RealtimeSpectrogram::RealtimeSpectrogram(const SpectrogramConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_frameCache(config.cacheSize)
    , m_hasOverlap(false)
{
    initializeFFT();

    // Timer para mantenimiento del cache
    m_maintenanceTimer = new QTimer(this);
    m_maintenanceTimer->setInterval(5000); // 5 segundos
    connect(m_maintenanceTimer, &QTimer::timeout, this, &RealtimeSpectrogram::performMaintenance);
    m_maintenanceTimer->start();

    qDebug() << "RealtimeSpectrogram inicializado - FFT Size:" << m_config.fftSize;
}

RealtimeSpectrogram::~RealtimeSpectrogram()
{
    clearCache();
}

void RealtimeSpectrogram::setConfig(const SpectrogramConfig& config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;
    m_frameCache.setMaxCost(config.cacheSize);
    initializeFFT();

    qDebug() << "Configuración actualizada - FFT:" << config.fftSize
             << "Overlap:" << config.overlap << "Cache:" << config.cacheSize;
}

bool RealtimeSpectrogram::processAudioBlock(qint64 blockIndex, const QByteArray& audioData, qint64 timestamp)
{
    if (audioData.isEmpty()) {
        emit errorOccurred("Bloque de audio vacío");
        return false;
    }

    if (timestamp == 0) {
        timestamp = QDateTime::currentMSecsSinceEpoch();
    }

    QMutexLocker locker(&m_mutex);

    try {
        // Convertir datos de audio a float
        QVector<float> samples = convertToFloat(audioData);

        if (samples.size() < m_config.fftSize) {
            qWarning() << "Bloque muy pequeño para FFT:" << samples.size() << "< " << m_config.fftSize;
            return false;
        }

        // Procesar con solapamiento si está configurado
        int hopSize = m_config.fftSize - m_config.overlap;
        int processedSamples = 0;

        while (processedSamples + m_config.fftSize <= samples.size()) {
            // Extraer ventana de muestras
            QVector<float> windowSamples(m_config.fftSize);
            for (int i = 0; i < m_config.fftSize; ++i) {
                windowSamples[i] = samples[processedSamples + i];
            }

            // Aplicar ventana
            applyWindow(windowSamples);

            // Calcular FFT
            QVector<float> magnitudes = computeFFT(windowSamples);

            // Aplicar suavizado temporal si está habilitado
            if (m_config.enableSmoothing && !m_previousFrame.isEmpty()) {
                applySmoothingFilter(magnitudes);
            }

            // Crear frame del espectrograma
            SpectrogramFrame frame;
            frame.blockIndex = blockIndex;
            frame.timestamp = timestamp + (processedSamples * 1000 / m_config.sampleRate);
            frame.magnitudes = magnitudes;
            frame.frequencies = getFrequencyAxis();
            frame.sampleRate = m_config.sampleRate;
            frame.fftSize = m_config.fftSize;

            // Añadir al cache
            addFrameToCache(frame);

            // Guardar para suavizado
            if (m_config.enableSmoothing) {
                m_previousFrame = magnitudes;
            }

            emit frameReady(frame);

            processedSamples += hopSize;
        }

        return true;

    } catch (const std::exception& e) {
        QString error = QString("Error procesando bloque %1: %2").arg(blockIndex).arg(e.what());
        emit errorOccurred(error);
        return false;
    }
}

QVector<SpectrogramFrame> RealtimeSpectrogram::getFramesInRange(qint64 startTime, qint64 endTime) const
{
    QMutexLocker locker(&m_mutex);
    QVector<SpectrogramFrame> result;

    for (qint64 key : m_frameOrder) {
        SpectrogramFrame* frame = m_frameCache.object(key);
        if (frame && frame->timestamp >= startTime &&
            (endTime == -1 || frame->timestamp <= endTime)) {
            result.append(*frame);
        }
    }

    return result;
}

SpectrogramFrame RealtimeSpectrogram::getLatestFrame() const
{
    QMutexLocker locker(&m_mutex);

    if (m_frameOrder.isEmpty()) {
        return SpectrogramFrame();
    }

    qint64 latestKey = m_frameOrder.last();
    SpectrogramFrame* frame = m_frameCache.object(latestKey);

    return frame ? *frame : SpectrogramFrame();
}

QVector<QVector<float>> RealtimeSpectrogram::getSpectrogramMatrix(qint64 startTime, qint64 endTime) const
{
    QVector<SpectrogramFrame> frames = getFramesInRange(startTime, endTime);
    QVector<QVector<float>> matrix;

    for (const auto& frame : frames) {
        matrix.append(frame.magnitudes);
    }

    return matrix;
}

QVector<float> RealtimeSpectrogram::getFrequencyAxis() const
{
    QVector<float> frequencies(m_config.fftSize / 2 + 1);
    float freqStep = static_cast<float>(m_config.sampleRate) / m_config.fftSize;

    for (int i = 0; i < frequencies.size(); ++i) {
        frequencies[i] = i * freqStep;
    }

    return frequencies;
}

void RealtimeSpectrogram::clearCache()
{
    QMutexLocker locker(&m_mutex);
    m_frameCache.clear();
    m_frameOrder.clear();
    m_previousFrame.clear();
    emit cacheUpdated(0);
}

int RealtimeSpectrogram::getCachedFrameCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_frameOrder.size();
}

QString RealtimeSpectrogram::getStatistics() const
{
    QMutexLocker locker(&m_mutex);

    int frameCount = m_frameOrder.size();
    float durationSeconds = 0.0f;
    float memoryUsageMB = 0.0f;

    if (frameCount > 0) {
        SpectrogramFrame* first = m_frameCache.object(m_frameOrder.first());
        SpectrogramFrame* last = m_frameCache.object(m_frameOrder.last());

        if (first && last) {
            durationSeconds = (last->timestamp - first->timestamp) / 1000.0f;
        }

        // Estimar uso de memoria
        int bytesPerFrame = sizeof(SpectrogramFrame) +
                            (m_config.fftSize / 2 + 1) * sizeof(float) * 2; // magnitudes + frequencies
        memoryUsageMB = (frameCount * bytesPerFrame) / (1024.0f * 1024.0f);
    }

    return QString("Estadísticas Espectrograma:\n"
                   "- Frames en caché: %1\n"
                   "- Duración total: %.2f segundos\n"
                   "- Tamaño FFT: %2\n"
                   "- Solapamiento: %3\n"
                   "- Uso de memoria: %.2f MB\n"
                   "- Suavizado: %4")
        .arg(frameCount)
        .arg(m_config.fftSize)
        .arg(m_config.overlap)
        .arg(memoryUsageMB)
        .arg(durationSeconds)
        .arg(m_config.enableSmoothing ? "Habilitado" : "Deshabilitado");
}

void RealtimeSpectrogram::performMaintenance()
{
    cleanupOldFrames();
}

void RealtimeSpectrogram::initializeFFT()
{
    m_fftBuffer.resize(m_config.fftSize);
    generateWindow();
    m_overlapBuffer.clear();
    m_overlapBuffer.resize(m_config.overlap, 0.0f);
    m_hasOverlap = false;
}

void RealtimeSpectrogram::generateWindow()
{
    m_window.resize(m_config.fftSize);

    for (int i = 0; i < m_config.fftSize; ++i) {
        float n = static_cast<float>(i);
        float N = static_cast<float>(m_config.fftSize - 1);

        switch (static_cast<int>(m_config.windowType)) {
        case 1: // Hamming
            m_window[i] = 0.54f - 0.46f * qCos(2.0f * M_PI * n / N);
            break;
        case 2: // Blackman
            m_window[i] = 0.42f - 0.5f * qCos(2.0f * M_PI * n / N) +
                          0.08f * qCos(4.0f * M_PI * n / N);
            break;
        default: // Hann
            m_window[i] = 0.5f * (1.0f - qCos(2.0f * M_PI * n / N));
            break;
        }
    }
}

QVector<float> RealtimeSpectrogram::computeFFT(const QVector<float>& samples)
{
    // Copiar muestras al buffer FFT
    for (int i = 0; i < qMin(samples.size(), m_config.fftSize); ++i) {
        m_fftBuffer[i] = std::complex<float>(samples[i], 0.0f);
    }

    // Rellenar con ceros si es necesario
    for (int i = samples.size(); i < m_config.fftSize; ++i) {
        m_fftBuffer[i] = std::complex<float>(0.0f, 0.0f);
    }

    // Realizar FFT
    performFFT(m_fftBuffer);

    // Calcular magnitudes
    return calculateMagnitudes(m_fftBuffer);
}

QVector<float> RealtimeSpectrogram::convertToFloat(const QByteArray& audioData) const
{
    QVector<float> samples;
    const int16_t* data = reinterpret_cast<const int16_t*>(audioData.constData());
    int sampleCount = audioData.size() / sizeof(int16_t);

    samples.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        // Normalizar de int16 a float [-1.0, 1.0]
        samples.append(static_cast<float>(data[i]) / 32768.0f);
    }

    return samples;
}

void RealtimeSpectrogram::applyWindow(QVector<float>& samples)
{
    for (int i = 0; i < qMin(samples.size(), m_window.size()); ++i) {
        samples[i] *= m_window[i];
    }
}

QVector<float> RealtimeSpectrogram::calculateMagnitudes(const QVector<std::complex<float>>& fftResult)
{
    QVector<float> magnitudes;
    int numBins = m_config.fftSize / 2 + 1; // Solo la mitad positiva del espectro

    magnitudes.reserve(numBins);
    for (int i = 0; i < numBins; ++i) {
        float magnitude = std::abs(fftResult[i]);
        // Convertir a dB
        float dB = 20.0f * log10f(qMax(magnitude, 1e-10f));
        magnitudes.append(dB);
    }

    return magnitudes;
}

void RealtimeSpectrogram::applySmoothingFilter(QVector<float>& magnitudes)
{
    if (m_previousFrame.size() != magnitudes.size()) {
        return;
    }

    float alpha = m_config.smoothingFactor;
    for (int i = 0; i < magnitudes.size(); ++i) {
        magnitudes[i] = alpha * m_previousFrame[i] + (1.0f - alpha) * magnitudes[i];
    }
}

void RealtimeSpectrogram::addFrameToCache(const SpectrogramFrame& frame)
{
    qint64 key = frame.timestamp;
    m_frameCache.insert(key, new SpectrogramFrame(frame));
    m_frameOrder.append(key);

    emit cacheUpdated(m_frameOrder.size());
}

void RealtimeSpectrogram::cleanupOldFrames()
{
    QMutexLocker locker(&m_mutex);

    // Limpiar frames muy antiguos (más de 1 hora)
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    qint64 maxAge = 3600000; // 1 hora en ms

    auto it = m_frameOrder.begin();
    while (it != m_frameOrder.end()) {
        if (currentTime - *it > maxAge) {
            m_frameCache.remove(*it);
            it = m_frameOrder.erase(it);
        } else {
            ++it;
        }
    }
}

void RealtimeSpectrogram::performFFT(QVector<std::complex<float>>& data)
{
    int N = data.size();

    // Bit-reverse
    bitReverse(data);

    // FFT iterativa
    for (int len = 2; len <= N; len *= 2) {
        float angle = -2.0f * M_PI / len;
        std::complex<float> wlen(qCos(angle), qSin(angle));

        for (int i = 0; i < N; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = data[i + j];
                std::complex<float> v = data[i + j + len / 2] * w;
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

void RealtimeSpectrogram::bitReverse(QVector<std::complex<float>>& data)
{
    int N = data.size();
    int j = 0;

    for (int i = 1; i < N; ++i) {
        int bit = N >> 1;
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
