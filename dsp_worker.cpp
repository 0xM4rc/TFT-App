#include "dsp_worker.h"
#include "spectrogram_calculator.h"
#include "audio_db.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <complex>
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

    if (m_cfg.hopSize <= 0) {
        qWarning() << "DSPWorker: hopSize inválido, usando fftSize/2";
        m_cfg.hopSize = m_cfg.fftSize / 2;
    }

    // Inicializar calculador de espectrograma
    initializeSpectrogramCalculator();

    qDebug() << "DSPWorker inicializado:"
             << "blockSize=" << m_cfg.blockSize
             << "fftSize=" << m_cfg.fftSize
             << "hopSize=" << m_cfg.hopSize
             << "sampleRate=" << m_cfg.sampleRate
             << "windowType=" << m_cfg.windowType;
}

DSPWorker::~DSPWorker() {
    // Limpiar recursos
    m_accumBuffer.clear();
    m_hanningWindow.clear();
    m_spectrogramCalc.reset();
}

DSPConfig DSPWorker::getConfig() const {
    return m_cfg;
}

void DSPWorker::setConfig(const DSPConfig& cfg) {
    bool needsSpectrogramUpdate = (
        cfg.fftSize != m_cfg.fftSize ||
        cfg.hopSize != m_cfg.hopSize ||
        cfg.sampleRate != m_cfg.sampleRate ||
        cfg.windowType != m_cfg.windowType ||
        cfg.kaiserBeta != m_cfg.kaiserBeta ||
        cfg.gaussianSigma != m_cfg.gaussianSigma ||
        cfg.logScale != m_cfg.logScale ||
        cfg.noiseFloor != m_cfg.noiseFloor
        );

    m_cfg = cfg;

    if (needsSpectrogramUpdate) {
        updateSpectrogramConfig();
    }

    // Limpiar ventana legacy si cambia el tamaño
    if (cfg.fftSize != m_cfg.fftSize) {
        m_windowCalculated = false;
        m_hanningWindow.clear();
    }
}

qint64 DSPWorker::getTotalSamples() const {
    return m_totalSamples;
}

qint64 DSPWorker::getBlockIndex() const {
    return m_blockIndex;
}

int DSPWorker::getAccumBufferSize() const {
    return m_accumBuffer.size();
}

QVector<float> DSPWorker::getFrequencyBins() const {
    if (m_spectrogramCalc) {
        return m_spectrogramCalc->getFrequencyBins();
    }

    // Fallback: calcular frecuencias manualmente
    int bins = m_cfg.fftSize / 2 + 1;
    QVector<float> freqs(bins);
    float freqStep = static_cast<float>(m_cfg.sampleRate) / m_cfg.fftSize;

    for (int i = 0; i < bins; ++i) {
        freqs[i] = i * freqStep;
    }

    return freqs;
}

QString DSPWorker::getStatusInfo() const {
    return QString("DSPWorker: %1 bloques, %2 muestras, buffer: %3")
    .arg(m_blockIndex)
        .arg(m_totalSamples)
        .arg(m_accumBuffer.size());
}

QString DSPWorker::getSpectrogramInfo() const {
    if (m_spectrogramCalc) {
        return m_spectrogramCalc->getWindowInfo();
    }
    return "SpectrogramCalculator no disponible";
}

void DSPWorker::processChunk(const QVector<float>& samples, qint64 timestamp) {
    if (samples.isEmpty()) {
        emit errorOccurred("Chunk de muestras vacío");
        return;
    }

    if (m_cfg.blockSize <= 0 || m_cfg.sampleRate <= 0) {
        emit errorOccurred("Configuración inválida (blockSize o sampleRate ≤ 0)");
        return;
    }

    try {
        // Si es el primer bloque de toda la sesión, fijamos el origin timestamp
        if (m_startTimestamp < 0) {
            m_startTimestamp = timestamp;
        }

        // Acumulamos muestras
        m_accumBuffer += samples;

        // Preparamos batch de frames
        QVector<FrameData> batch;

        // Procesamos todos los bloques completos
        while (m_accumBuffer.size() >= m_cfg.blockSize) {
            QVector<float> block = m_accumBuffer.mid(0, m_cfg.blockSize);
            m_accumBuffer.erase(
                m_accumBuffer.begin(),
                m_accumBuffer.begin() + m_cfg.blockSize
                );

            // Calculamos el timestamp para este bloque
            qint64 deltaMs = qRound(1000.0 *
                                    (static_cast<double>(m_totalSamples) / m_cfg.sampleRate));
            qint64 blockTs = m_startTimestamp + deltaMs;

            // Procesamos el bloque y lo agregamos al batch
            FrameData frame = processBlock(block, blockTs, m_totalSamples);
            batch.append(frame);

            // Guardamos en la base de datos
            saveFrameToDb(frame, m_blockIndex);

            // Actualizamos contadores
            m_totalSamples += m_cfg.blockSize;
            ++m_blockIndex;
        }

        // Emitimos el batch si hay frames procesados
        if (!batch.isEmpty()) {
            emit framesReady(batch);
        }

        // Estadísticas periódicas
        if (m_blockIndex % 100 == 0) {
            emit statsUpdated(m_blockIndex, m_totalSamples, m_accumBuffer.size());
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

    if (m_cfg.sampleRate <= 0) {
        emit errorOccurred("sampleRate inválido para flushResidual");
        return;
    }

    try {
        qDebug() << "DSPWorker: procesando" << m_accumBuffer.size() << "muestras residuales";

        // Timestamp para el último trozo
        qint64 deltaMs = qRound(1000.0 *
                                (static_cast<double>(m_totalSamples) / m_cfg.sampleRate));
        qint64 blockTs = m_startTimestamp + deltaMs;

        // Procesamos el bloque residual
        FrameData frame = processBlock(m_accumBuffer, blockTs, m_totalSamples);

        // Guardamos en la base de datos
        saveFrameToDb(frame, m_blockIndex);

        // Emitimos como batch de un solo frame
        QVector<FrameData> batch;
        batch.append(frame);
        emit framesReady(batch);

        m_totalSamples += m_accumBuffer.size();
        m_accumBuffer.clear();

        // Estadísticas finales
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
    m_startTimestamp = -1;

    // Reinicializar calculador de espectrograma
    initializeSpectrogramCalculator();

    emit statsUpdated(0, 0, 0);
}

FrameData DSPWorker::processBlock(const QVector<float>& block,
                                  qint64 timestamp,
                                  qint64 sampleOffset) {
    FrameData frame;
    frame.timestamp = timestamp;
    frame.sampleOffset = sampleOffset;

    if (block.isEmpty()) {
        return frame;
    }

    try {
        // --- Waveform (down-sample a waveformSize muestras) ---
        if (m_cfg.enablePeaks) {
            int N = block.size();
            int W = m_cfg.waveformSize > 0 ? m_cfg.waveformSize : 1;
            frame.waveform.resize(W);

            // Uniform down-sample
            for (int i = 0; i < W; ++i) {
                int idx = qMin(N - 1, (i * N) / W);
                frame.waveform[i] = block[idx];
            }
        } else {
            // Si no necesitamos peaks, guardamos solo la primera muestra
            frame.waveform.resize(1);
            frame.waveform[0] = block[0];
        }

        // --- Espectrograma usando SpectrogramCalculator ---
        if (m_cfg.enableSpectrum && m_spectrogramCalc) {
            auto spectrogramFrame = m_spectrogramCalc->calculateFrame(block, timestamp, sampleOffset);
            frame.spectrum = spectrogramFrame.magnitudes;
            frame.frequencies = spectrogramFrame.frequencies;
            frame.windowGain = spectrogramFrame.windowGain;
        } else if (m_cfg.enableSpectrum) {
            // Fallback al método legacy
            frame.spectrum = calculateSpectrum(block);
            frame.frequencies = getFrequencyBins();
            frame.windowGain = 1.0f;
        }

        // --- Guardar bloque raw en la base de datos ---
        if (m_db) {
            QByteArray blob(reinterpret_cast<const char*>(block.constData()),
                            block.size() * sizeof(float));
            m_db->insertBlock(m_blockIndex, sampleOffset, blob);
        }

    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error procesando bloque: %1").arg(e.what()));
    }

    return frame;
}

void DSPWorker::saveFrameToDb(const FrameData& frame, qint64 blockIndex) {
    if (!m_db) return;

    try {
        // Guardar picos si están habilitados
        if (m_cfg.enablePeaks && frame.waveform.size() >= 2) {
            m_db->insertPeak(blockIndex, frame.sampleOffset, frame.waveform[0], frame.waveform[1]);
        }

        // Nota: El bloque raw ya se guardó en processBlock()
        // para mantener el orden correcto de las operaciones

    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error guardando en DB: %1").arg(e.what()));
    }
}

void DSPWorker::initializeSpectrogramCalculator() {
    try {
        SpectrogramConfig spectrogramConfig;
        spectrogramConfig.fftSize = m_cfg.fftSize;
        spectrogramConfig.hopSize = m_cfg.hopSize;
        spectrogramConfig.sampleRate = m_cfg.sampleRate;
        spectrogramConfig.windowType = static_cast<WindowType>(m_cfg.windowType);
        spectrogramConfig.kaiserBeta = m_cfg.kaiserBeta;
        spectrogramConfig.gaussianSigma = m_cfg.gaussianSigma;
        spectrogramConfig.logScale = m_cfg.logScale;
        spectrogramConfig.noiseFloor = m_cfg.noiseFloor;

        m_spectrogramCalc = std::make_unique<SpectrogramCalculator>(spectrogramConfig, this);

        // Conectar señales de error
        connect(m_spectrogramCalc.get(), &SpectrogramCalculator::errorOccurred,
                this, &DSPWorker::errorOccurred);

        qDebug() << "SpectrogramCalculator inicializado:" << m_spectrogramCalc->getWindowInfo();
    }
    catch (const std::exception& e) {
        emit errorOccurred(QString("Error inicializando SpectrogramCalculator: %1").arg(e.what()));
    }
}

void DSPWorker::updateSpectrogramConfig() {
    if (!m_spectrogramCalc) {
        initializeSpectrogramCalculator();
        return;
    }

    try {
        SpectrogramConfig spectrogramConfig;
        spectrogramConfig.fftSize = m_cfg.fftSize;
        spectrogramConfig.hopSize = m_cfg.hopSize;
        spectrogramConfig.sampleRate = m_cfg.sampleRate;
        spectrogramConfig.windowType = static_cast<WindowType>(m_cfg.windowType);
        spectrogramConfig.kaiserBeta = m_cfg.kaiserBeta;
        spectrogramConfig.gaussianSigma = m_cfg.gaussianSigma;
        spectrogramConfig.logScale = m_cfg.logScale;
        spectrogramConfig.noiseFloor = m_cfg.noiseFloor;

        m_spectrogramCalc->setConfig(spectrogramConfig);

        qDebug() << "SpectrogramCalculator actualizado:" << m_spectrogramCalc->getWindowInfo();
    }
    catch (const std::exception& e) {
        emit errorOccurred(QString("Error actualizando SpectrogramCalculator: %1").arg(e.what()));
    }
}

// Métodos legacy mantenidos para compatibilidad
void DSPWorker::handleBlock(const QVector<float>& block, qint64 timestamp) {
    // Método mantenido para compatibilidad, pero ya no se usa
    // Todo el procesamiento se hace ahora en processBlock
}

void DSPWorker::calculateSpectrum(const QVector<float>& block, qint64 timestamp) {
    // Método mantenido para compatibilidad, pero ya no se usa
    // Todo el procesamiento se hace ahora en processBlock
}

QVector<float> DSPWorker::calculateSpectrum(const QVector<float>& block) {
    // Método legacy para compatibilidad
    int N = m_cfg.fftSize;
    QVector<float> magnitudes;

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

    // Este es el método simplificado - en producción se usaría FFTW
    int bins = N / 2 + 1;
    magnitudes.resize(bins);

    // Simulación simple de FFT (reemplazar con FFTW real)
    for (int i = 0; i < bins; ++i) {
        magnitudes[i] = std::abs(in[i % in.size()]);
        if (magnitudes[i] > 0.0f) {
            magnitudes[i] = 20.0f * std::log10f(magnitudes[i]);
        } else {
            magnitudes[i] = -100.0f;
        }
    }

    return magnitudes;
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
