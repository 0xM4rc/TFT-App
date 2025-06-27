#include "dsp_worker.h"


#include <algorithm>
#include <complex>
#include <fftw3.h>  // o KissFFT, según prefieras

DSPWorker::DSPWorker(const DSPConfig& cfg, AudioDb* db, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_db(db)
{
    // Para hacer flush periódico (por ejemplo, volcar buffers o emitir heartbeats)
    // connect(&m_flushTimer, &QTimer::timeout, this, &DSPWorker::onFlushTimer);
    // m_flushTimer.start(1000);
}

DSPWorker::~DSPWorker() {
    // nada especial
}

void DSPWorker::processChunk(const QByteArray& pcmBytes, qint64 timestamp) {
    // 1) Convertir bytes a floats normalizados [-1,1]
    QVector<float> samples = bytesToFloats(pcmBytes);

    // 2) Append al acumulador
    m_accumBuffer += samples;

    // 3) Mientras tengamos al menos blockSize muestras, procesa bloques
    while (m_accumBuffer.size() >= m_cfg.blockSize) {
        // Extraer el bloque de waveform
        QVector<float> block = m_accumBuffer.mid(0, m_cfg.blockSize);
        m_accumBuffer.remove(0, m_cfg.blockSize);

        qint64 blockTs = timestamp;  // o calcula a partir de m_totalSamples

        handleBlock(block, blockTs);

        m_totalSamples += m_cfg.blockSize;
        ++m_blockIndex;
    }
}

void DSPWorker::handleBlock(const QVector<float>& blockSamples, qint64 timestamp) {
    // --- Waveform (min/max) ---
    auto [mnIt, mxIt] = std::minmax_element(blockSamples.begin(), blockSamples.end());
    float mn = *mnIt, mx = *mxIt;
    emit minMaxReady(mn, mx, timestamp);

    // Graba en la DB
    QByteArray pcmBlob(reinterpret_cast<const char*>(blockSamples.constData()),
                       blockSamples.size() * sizeof(float));
    m_db->insertBlock(m_blockIndex, m_totalSamples, pcmBlob);
    m_db->insertPeak(m_blockIndex, m_totalSamples, mn, mx);

    // --- Espectrograma (FFT de tamaño fftSize) ---
    // Si blockSize >= fftSize, tomamos la primera ventana; o acumula internamente solapamiento
    QVector<float> window( m_cfg.fftSize );
    // Hann window
    for (int i = 0; i < m_cfg.fftSize; ++i)
        window[i] = 0.5f * (1 - cosf(2*M_PI*i/(m_cfg.fftSize-1)));

    // Prepara buffers
    QVector<std::complex<float>> fftIn(m_cfg.fftSize), fftOut(m_cfg.fftSize);
    for (int i = 0; i < m_cfg.fftSize; ++i)
        fftIn[i] = blockSamples[i] * window[i];

    // Ejecuta FFT (por FFTW, KissFFT…)
    fftwf_plan plan = fftwf_plan_dft_r2c_1d(m_cfg.fftSize,
                                            reinterpret_cast<float*>(fftIn.data()),
                                            reinterpret_cast<fftwf_complex*>(fftOut.data()),
                                            FFTW_ESTIMATE);
    fftwf_execute(plan);
    fftwf_destroy_plan(plan);

    // Extrae magnitud (solo mitad positiva)
    int bins = m_cfg.fftSize/2 + 1;
    QVector<float> mag(bins);
    for (int i = 0; i < bins; ++i)
        mag[i] = std::abs(fftOut[i]);

    emit specColumnReady(mag, timestamp);

    // (Opcional) guardar columna en DB
    QByteArray specBlob(reinterpret_cast<const char*>(mag.constData()),
                        mag.size() * sizeof(float));
    // m_db->insertSpectrogram(m_blockIndex, m_totalSamples, specBlob);
}

QVector<float> DSPWorker::bytesToFloats(const QByteArray& pcm) const {
    // Asume Int16 PCM; ajusta a tu formato real
    int sampleCount = pcm.size()/2;
    QVector<float> out(sampleCount);
    const qint16* in = reinterpret_cast<const qint16*>(pcm.constData());
    for (int i = 0; i < sampleCount; ++i)
        out[i] = in[i] / 32768.0f;
    return out;
}

// Si necesitas periodic polling
void DSPWorker::onFlushTimer() {
    // Podrías emitir un estado o vaciar buffers débiles aquí
}
