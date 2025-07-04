#include "spectrogram_calculator.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <fftw3.h>

SpectrogramCalculator::SpectrogramCalculator(const SpectrogramConfig& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_windowGain(1.0f)
    , m_windowNeedsUpdate(true)
    , m_frequenciesNeedUpdate(true)
{
    // Validar configuración
    if (m_config.fftSize <= 0) {
        qWarning() << "SpectrogramCalculator: fftSize inválido, usando 1024";
        m_config.fftSize = 1024;
        m_windowNeedsUpdate = true;
    }

    if (m_config.hopSize <= 0) {
        qWarning() << "SpectrogramCalculator: hopSize inválido, usando fftSize/2";
        m_config.hopSize = m_config.fftSize / 2;
    }

    if (m_config.sampleRate <= 0) {
        qWarning() << "SpectrogramCalculator: sampleRate inválido, usando 44100";
        m_config.sampleRate = 44100;
        m_frequenciesNeedUpdate = true;
    }

    qDebug() << "SpectrogramCalculator inicializado:"
             << "fftSize=" << m_config.fftSize
             << "hopSize=" << m_config.hopSize
             << "sampleRate=" << m_config.sampleRate
             << "window=" << windowTypeToString(m_config.windowType);
}

SpectrogramCalculator::~SpectrogramCalculator() {
    m_window.clear();
    m_frequencies.clear();
}

void SpectrogramCalculator::setConfig(const SpectrogramConfig& config) {
    bool windowChanged = (config.fftSize != m_config.fftSize ||
                          config.windowType != m_config.windowType ||
                          config.kaiserBeta != m_config.kaiserBeta ||
                          config.gaussianSigma != m_config.gaussianSigma);

    bool freqChanged = (config.sampleRate != m_config.sampleRate ||
                        config.fftSize != m_config.fftSize);

    m_config = config;

    if (windowChanged) {
        m_windowNeedsUpdate = true;
    }

    if (freqChanged) {
        m_frequenciesNeedUpdate = true;
    }
}

SpectrogramFrame SpectrogramCalculator::calculateFrame(const QVector<float>& samples,
                                                       qint64 timestamp,
                                                       qint64 sampleOffset) {
    SpectrogramFrame frame;
    frame.timestamp = timestamp;
    frame.sampleOffset = sampleOffset;

    if (samples.isEmpty()) {
        emit errorOccurred("Muestras vacías para cálculo de espectrograma");
        return frame;
    }

    try {
        // Actualizar ventana si es necesario
        if (m_windowNeedsUpdate) {
            updateWindow();
        }

        // Actualizar frecuencias si es necesario
        if (m_frequenciesNeedUpdate) {
            updateFrequencies();
        }

        // Preparar datos con zero-padding si es necesario
        QVector<float> data(m_config.fftSize, 0.0f);
        int copySize = std::min(static_cast<int>(samples.size()),
                                m_config.fftSize);

        // Copiar muestras
        for (int i = 0; i < copySize; ++i) {
            data[i] = samples[i];
        }

        // Aplicar ventana
        QVector<float> windowedData = applyWindow(data);

        // Calcular FFT
        frame.magnitudes = applyFFT(windowedData);
        frame.frequencies = m_frequencies;
        frame.windowGain = m_windowGain;

    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error calculando espectrograma: %1").arg(e.what()));
    }

    return frame;
}

QVector<SpectrogramFrame> SpectrogramCalculator::processOverlapped(const QVector<float>& samples,
                                                                   qint64 startTimestamp,
                                                                   qint64 startOffset) {
    QVector<SpectrogramFrame> frames;

    if (samples.size() < m_config.fftSize) {
        // Si no hay suficientes muestras, procesamos lo que hay
        SpectrogramFrame frame = calculateFrame(samples, startTimestamp, startOffset);
        frames.append(frame);
        return frames;
    }

    int numFrames = (samples.size() - m_config.fftSize) / m_config.hopSize + 1;

    for (int i = 0; i < numFrames; ++i) {
        int startIdx = i * m_config.hopSize;
        int endIdx = std::min(startIdx + m_config.fftSize,
                            static_cast<int>(samples.size()));


        QVector<float> frameData = samples.mid(startIdx, endIdx - startIdx);

        // Calcular timestamp para este frame
        qint64 frameTimestamp = startTimestamp +
                                qRound(1000.0 * startIdx / m_config.sampleRate);
        qint64 frameOffset = startOffset + startIdx;

        SpectrogramFrame frame = calculateFrame(frameData, frameTimestamp, frameOffset);
        frames.append(frame);
    }

    return frames;
}

QVector<float> SpectrogramCalculator::getFrequencyBins() const {
    if (m_frequenciesNeedUpdate) {
        const_cast<SpectrogramCalculator*>(this)->updateFrequencies();
    }
    return m_frequencies;
}

QString SpectrogramCalculator::getWindowInfo() const {
    QString info = QString("Window: %1, Size: %2, Gain: %3")
    .arg(windowTypeToString(m_config.windowType))
        .arg(m_config.fftSize)
        .arg(m_windowGain, 0, 'f', 3);

    if (m_config.windowType == WindowType::Kaiser) {
        info += QString(", Beta: %1").arg(m_config.kaiserBeta);
    } else if (m_config.windowType == WindowType::Gaussian) {
        info += QString(", Sigma: %1").arg(m_config.gaussianSigma);
    }

    return info;
}

void SpectrogramCalculator::updateWindow() {
    m_window = calculateWindow(m_config.windowType, m_config.fftSize,
                               m_config.kaiserBeta, m_config.gaussianSigma);
    m_windowGain = calculateWindowGain(m_window);
    m_windowNeedsUpdate = false;

    qDebug() << "Ventana actualizada:" << windowTypeToString(m_config.windowType)
             << "Ganancia:" << m_windowGain;
}

void SpectrogramCalculator::updateFrequencies() {
    int bins = m_config.fftSize / 2 + 1;
    m_frequencies.resize(bins);

    float freqStep = static_cast<float>(m_config.sampleRate) / m_config.fftSize;

    for (int i = 0; i < bins; ++i) {
        m_frequencies[i] = i * freqStep;
    }

    m_frequenciesNeedUpdate = false;
}

QVector<float> SpectrogramCalculator::applyFFT(const QVector<float>& windowedData) {
    int N = windowedData.size();
    int bins = N / 2 + 1;

    QVector<float> magnitudes(bins);

    // Preparar memoria para FFTW
    fftwf_complex* out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * bins);
    if (!out) {
        emit errorOccurred("Error allocando memoria para FFT");
        return magnitudes;
    }

    // Crear plan FFT
    fftwf_plan plan = fftwf_plan_dft_r2c_1d(N,
                                            const_cast<float*>(windowedData.constData()),
                                            out,
                                            FFTW_ESTIMATE);
    if (!plan) {
        fftwf_free(out);
        emit errorOccurred("Error creando plan FFT");
        return magnitudes;
    }

    // Ejecutar FFT
    fftwf_execute(plan);

    // Calcular magnitudes
    for (int i = 0; i < bins; ++i) {
        float real = out[i][0];
        float imag = out[i][1];
        magnitudes[i] = std::sqrt(real * real + imag * imag);

        // Normalizar por tamaño FFT y ganancia de ventana
        magnitudes[i] /= (N * m_windowGain);

        // Aplicar escala logarítmica si está habilitada
        if (m_config.logScale) {
            if (magnitudes[i] > 0.0f) {
                magnitudes[i] = 20.0f * std::log10f(magnitudes[i]);
            } else {
                magnitudes[i] = m_config.noiseFloor;
            }
        }
    }

    // Limpiar recursos
    fftwf_destroy_plan(plan);
    fftwf_free(out);

    return magnitudes;
}

QVector<float> SpectrogramCalculator::applyWindow(const QVector<float>& samples) {
    if (m_windowNeedsUpdate) {
        updateWindow();
    }

    int size = std::min(samples.size(), m_window.size());
    QVector<float> result(size);

    for (int i = 0; i < size; ++i) {
        result[i] = samples[i] * m_window[i];
    }

    return result;
}

float SpectrogramCalculator::calculateWindowGain(const QVector<float>& window) {
    if (window.isEmpty()) return 1.0f;

    float sum = 0.0f;
    for (float val : window) {
        sum += val;
    }

    return sum / window.size();
}

// Implementaciones de ventanas específicas

QVector<float> SpectrogramCalculator::calculateWindow(WindowType type, int size,
                                                      double kaiserBeta,
                                                      double gaussianSigma) {
    switch (type) {
    case WindowType::Rectangular:
        return calculateRectangular(size);
    case WindowType::Hann:
        return calculateHann(size);
    case WindowType::Hamming:
        return calculateHamming(size);
    case WindowType::Blackman:
        return calculateBlackman(size);
    case WindowType::Bartlett:
        return calculateBartlett(size);
    case WindowType::Kaiser:
        return calculateKaiser(size, kaiserBeta);
    case WindowType::BlackmanHarris:
        return calculateBlackmanHarris(size);
    case WindowType::Gaussian:
        return calculateGaussian(size, gaussianSigma);
    default:
        return calculateHann(size);
    }
}

QVector<float> SpectrogramCalculator::calculateRectangular(int size) {
    return QVector<float>(size, 1.0f);
}

QVector<float> SpectrogramCalculator::calculateHann(int size) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    for (int i = 0; i < size; ++i) {
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size - 1)));
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateHamming(int size) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    for (int i = 0; i < size; ++i) {
        window[i] = 0.54f - 0.46f * std::cos(2.0f * M_PI * i / (size - 1));
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateBlackman(int size) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    for (int i = 0; i < size; ++i) {
        float t = 2.0f * M_PI * i / (size - 1);
        window[i] = 0.42f - 0.5f * std::cos(t) + 0.08f * std::cos(2.0f * t);
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateBartlett(int size) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    for (int i = 0; i < size; ++i) {
        window[i] = 1.0f - 2.0f * std::abs(i - (size - 1) / 2.0f) / (size - 1);
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateKaiser(int size, double beta) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    // Función Bessel modificada de orden 0 (aproximación)
    auto besselI0 = [](double x) -> double {
        double sum = 1.0;
        double term = 1.0;
        double x2 = x * x / 4.0;

        for (int k = 1; k < 50; ++k) {
            term *= x2 / (k * k);
            sum += term;
            if (term < 1e-10) break;
        }

        return sum;
    };

    double i0Beta = besselI0(beta);

    for (int i = 0; i < size; ++i) {
        double t = 2.0 * i / (size - 1) - 1.0;
        double arg = beta * std::sqrt(1.0 - t * t);
        window[i] = besselI0(arg) / i0Beta;
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateBlackmanHarris(int size) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    for (int i = 0; i < size; ++i) {
        float t = 2.0f * M_PI * i / (size - 1);
        window[i] = a0 - a1 * std::cos(t) + a2 * std::cos(2.0f * t) - a3 * std::cos(3.0f * t);
    }

    return window;
}

QVector<float> SpectrogramCalculator::calculateGaussian(int size, double sigma) {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) window[0] = 1.0f;
        return window;
    }

    double center = (size - 1) / 2.0;
    double variance = sigma * sigma * (size - 1) * (size - 1) / 4.0;

    for (int i = 0; i < size; ++i) {
        double t = i - center;
        window[i] = std::exp(-0.5 * t * t / variance);
    }

    return window;
}

QString SpectrogramCalculator::windowTypeToString(WindowType type) {
    switch (type) {
    case WindowType::Rectangular: return "Rectangular";
    case WindowType::Hann: return "Hann";
    case WindowType::Hamming: return "Hamming";
    case WindowType::Blackman: return "Blackman";
    case WindowType::Bartlett: return "Bartlett";
    case WindowType::Kaiser: return "Kaiser";
    case WindowType::BlackmanHarris: return "Blackman-Harris";
    case WindowType::Gaussian: return "Gaussian";
    default: return "Unknown";
    }
}
