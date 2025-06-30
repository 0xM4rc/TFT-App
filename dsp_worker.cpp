#include "dsp_worker.h"
#include "audio_db.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <complex>
#include <fftw3.h>
#include <cmath>

DSPWorker::DSPWorker(const DSPConfig& cfg, AudioDb* db, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_db(db)
{
    if (!m_db) {
        qWarning() << "DSPWorker: AudioDb es nullptr";
    }

    // Validar configuración
    if (m_cfg.blockSize <= 0) {
        qWarning() << "DSPWorker: blockSize inválido, usando 1024";
        m_cfg.blockSize = 1024;
    }

    if (m_cfg.fftSize <= 0) {
        qWarning() << "DSPWorker: fftSize inválido, usando 1024";
        m_cfg.fftSize = 1024;
    }

    if (m_cfg.sampleRate <= 0) {
        qWarning() << "DSPWorker: sampleRate inválido, usando 44100";
        m_cfg.sampleRate = 44100;
    }

    qDebug() << "DSPWorker inicializado:"
             << "blockSize=" << m_cfg.blockSize
             << "fftSize=" << m_cfg.fftSize
             << "sampleRate=" << m_cfg.sampleRate;
}

DSPWorker::~DSPWorker() {
    // Limpiar recursos si es necesario
    m_accumBuffer.clear();
    m_hanningWindow.clear();
}

void DSPWorker::processChunk(const QVector<float>& samples, qint64 timestamp) {
    if (samples.isEmpty()) {
        emit errorOccurred("Chunk de muestras vacío");
        return;
    }

    if (m_cfg.blockSize <= 0) {
        emit errorOccurred("Tamaño de bloque inválido");
        return;
    }

    try {
        // 1) Acumular muestras nuevas
        m_accumBuffer += samples;

        // 2) Mientras haya suficiente para un bloque, procesar
        while (m_accumBuffer.size() >= m_cfg.blockSize) {
            QVector<float> block = m_accumBuffer.mid(0, m_cfg.blockSize);
            m_accumBuffer.erase(m_accumBuffer.begin(),
                                m_accumBuffer.begin() + m_cfg.blockSize);

            handleBlock(block, timestamp);
            m_totalSamples += m_cfg.blockSize;
            ++m_blockIndex;

            // Emitir estadísticas cada 100 bloques
            if (m_blockIndex % 100 == 0) {
                emit statsUpdated(m_blockIndex, m_totalSamples, m_accumBuffer.size());
            }
        }
    }
    catch (const std::exception& e) {
        emit errorOccurred(QString("Error procesando chunk: %1").arg(e.what()));
    }
}

void DSPWorker::flushResidual() {
    if (m_accumBuffer.isEmpty()) {
        return;
    }

    try {
        // Procesar el resto, aunque no llegue a blockSize
        qDebug() << "DSPWorker: procesando" << m_accumBuffer.size() << "muestras residuales";
        handleBlock(m_accumBuffer, QDateTime::currentMSecsSinceEpoch());
        m_accumBuffer.clear();

        // Emitir estadísticas finales
        emit statsUpdated(m_blockIndex, m_totalSamples, 0);
    }
    catch (const std::exception& e) {
        emit errorOccurred(QString("Error procesando residual: %1").arg(e.what()));
    }
}

void DSPWorker::reset() {
    qDebug() << "DSPWorker: reiniciando estado";

    m_accumBuffer.clear();
    m_totalSamples = 0;
    m_blockIndex = 0;
    m_windowCalculated = false;
    m_hanningWindow.clear();

    emit statsUpdated(0, 0, 0);
}

void DSPWorker::handleBlock(const QVector<float>& block, qint64 timestamp) {
    if (block.isEmpty()) {
        return;
    }

    try {
        // --- Waveform (min/max) ---
        if (m_cfg.enablePeaks) {
            auto [itMin, itMax] = std::minmax_element(block.begin(), block.end());
            float mn = *itMin;
            float mx = *itMax;
            emit minMaxReady(mn, mx, timestamp);

            // Guardar picos en la DB
            if (m_db) {
                m_db->insertPeak(m_blockIndex, m_totalSamples, mn, mx);
            }
        }

        // Guardar el bloque completo en la DB como float32
        if (m_db) {
            QByteArray blob(reinterpret_cast<const char*>(block.constData()),
                            block.size() * sizeof(float));
            m_db->insertBlock(m_blockIndex, m_totalSamples, blob);
        }

        // --- FFT para espectrograma ---
        if (m_cfg.enableSpectrum) {
            calculateSpectrum(block, timestamp);
        }
    }
    catch (const std::exception& e) {
        emit errorOccurred(QString("Error procesando bloque: %1").arg(e.what()));
    }
}

void DSPWorker::calculateSpectrum(const QVector<float>& block, qint64 timestamp) {
    int N = m_cfg.fftSize;

    // Calcular ventana de Hanning una sola vez
    if (!m_windowCalculated) {
        m_hanningWindow = calculateHanningWindow(N);
        m_windowCalculated = true;
        qDebug() << "DSPWorker: ventana de Hanning calculada para N=" << N;
    }

    // Preparar datos de entrada con zero-padding si es necesario
    QVector<float> in(N, 0.0f);
    int copySize = std::min<int>(block.size(), N);

    // Copiar y aplicar ventana
    for (int i = 0; i < copySize; ++i) {
        in[i] = block[i] * m_hanningWindow[i];
    }

    // Configurar FFT
    int bins = N / 2 + 1;
    fftwf_complex* out = (fftwf_complex*)fftwf_malloc(sizeof(fftwf_complex) * bins);

    if (!out) {
        emit errorOccurred("Error allocando memoria para FFT");
        return;
    }

    fftwf_plan plan = fftwf_plan_dft_r2c_1d(N, in.data(), out, FFTW_ESTIMATE);

    if (!plan) {
        fftwf_free(out);
        emit errorOccurred("Error creando plan FFT");
        return;
    }

    // Ejecutar FFT
    fftwf_execute(plan);

    // Calcular magnitudes
    QVector<float> magnitudes(bins);
    for (int i = 0; i < bins; ++i) {
        float real = out[i][0];
        float imag = out[i][1];
        magnitudes[i] = std::sqrt(real * real + imag * imag);

        // Normalizar por el tamaño de la FFT
        magnitudes[i] /= N;

        // Aplicar escala logarítmica si se desea
        if (magnitudes[i] > 0.0f) {
            magnitudes[i] = 20.0f * std::log10f(magnitudes[i]);
        } else {
            magnitudes[i] = -100.0f; // Piso de ruido
        }
    }

    // Limpiar recursos FFT
    fftwf_destroy_plan(plan);
    fftwf_free(out);

    // Emitir resultado
    emit specColumnReady(magnitudes, timestamp);
}

QVector<float> DSPWorker::calculateHanningWindow(int size) const {
    QVector<float> window(size);

    if (size <= 1) {
        if (size == 1) {
            window[0] = 1.0f;
        }
        return window;
    }

    for (int i = 0; i < size; ++i) {
        window[i] = 0.5f * (1.0f - std::cosf(2.0f * M_PI * i / (size - 1)));
    }

    return window;
}

QVector<float> DSPWorker::applyWindow(const QVector<float>& samples,
                                      const QVector<float>& window) const {
    int size = std::min(samples.size(), window.size());
    QVector<float> result(size);

    for (int i = 0; i < size; ++i) {
        result[i] = samples[i] * window[i];
    }

    return result;
}

// Método adicional para obtener información de frecuencias
QVector<float> DSPWorker::getFrequencyBins() const {
    int bins = m_cfg.fftSize / 2 + 1;
    QVector<float> freqs(bins);

    float freqStep = static_cast<float>(m_cfg.sampleRate) / m_cfg.fftSize;

    for (int i = 0; i < bins; ++i) {
        freqs[i] = i * freqStep;
    }

    return freqs;
}

// Método para obtener información del estado actual
QString DSPWorker::getStatusInfo() const {
    return QString("DSPWorker: %1 bloques, %2 muestras, buffer: %3")
    .arg(m_blockIndex)
        .arg(m_totalSamples)
        .arg(m_accumBuffer.size());
}
