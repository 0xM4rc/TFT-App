#include "dsp_worker.h"
#include "spectrogram_calculator.h"
#include "audio_db.h"
#include <QDateTime>
#include <QDebug>
#include <algorithm>
#include <cmath>

DSPWorker::DSPWorker(const DSPConfig& cfg, AudioDb* db, QObject* parent)
    : QObject(parent)
    , m_cfg(cfg)
    , m_db(db)
    , m_startTimestampNs(-1)  // Inicializar explícitamente
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

void DSPWorker::processChunk(const QVector<float>& samples, quint64 timestampNs) {
    qDebug() << "processChunk: offsetNs recibido =" << timestampNs;

    if (samples.isEmpty()) {
        emit errorOccurred("Chunk de muestras vacío");
        return;
    }
    if (m_cfg.blockSize <= 0 || m_cfg.sampleRate <= 0) {
        emit errorOccurred("Configuración inválida (blockSize o sampleRate ≤ 0)");
        return;
    }

    // 1) Guardar el offset inicial si aún no lo hemos hecho
    if (m_startTimestampNs < 0) {
        m_startTimestampNs = timestampNs;
        qDebug() << "DSPWorker: offset inicial establecido a" << m_startTimestampNs << "ns";
    }

    // 2) Acumular muestras
    m_accumBuffer += samples;

    // 3) Preparar el batch de frames
    QVector<FrameData> batch;
    const double nsPerSample = 1e9 / double(m_cfg.sampleRate);

    // 4) Procesar todos los bloques completos
    while (m_accumBuffer.size() >= m_cfg.blockSize) {
        // 4.1) Extraer un bloque
        QVector<float> block = m_accumBuffer.mid(0, m_cfg.blockSize);
        m_accumBuffer.erase(
            m_accumBuffer.begin(),
            m_accumBuffer.begin() + m_cfg.blockSize
            );

        // 4.2) Calcular timestamp del bloque como offset desde el inicio
        quint64 deltaNs   = static_cast<quint64>(m_totalSamples * nsPerSample);
        quint64 blockTsNs = m_startTimestampNs + deltaNs;

        // Debug: sólo los primeros 5 bloques
        if (m_blockIndex < 5) {
            qDebug() << "Bloque" << m_blockIndex
                     << "- offsetStart:" << m_startTimestampNs
                     << "deltaNs:" << deltaNs
                     << "blockOffsetNs:" << blockTsNs;
        }

        // 4.3) Procesar el bloque
        FrameData frame = processBlock(block, blockTsNs, m_totalSamples);
        batch.append(frame);

        // 4.4) Guardar en la base de datos usando este offset
        saveFrameToDb(frame, m_blockIndex);

        // 4.5) Actualizar contadores
        m_totalSamples += m_cfg.blockSize;
        ++m_blockIndex;
    }

    // 5) Emitir los frames procesados
    if (!batch.isEmpty()) {
        emit framesReady(batch);
    }

    // 6) Estadísticas cada 100 bloques
    if (m_blockIndex % 100 == 0) {
        emit statsUpdated(m_blockIndex, m_totalSamples, m_accumBuffer.size());
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

    // Verificar que tenemos un timestamp válido
    if (m_startTimestampNs < 0) {
        qWarning() << "DSPWorker: flushResidual sin timestamp válido, usando timestamp actual";
        m_startTimestampNs = getCurrentTimestampNs();
    }

    try {
        qDebug() << "DSPWorker: procesando" << m_accumBuffer.size() << "muestras residuales";

        // Calcular timestamp para el último trozo en nanosegundos
        const double nsPerSample = 1e9 / double(m_cfg.sampleRate);
        quint64 deltaNs = static_cast<quint64>(m_totalSamples * nsPerSample);
        quint64 blockTsNs = m_startTimestampNs + deltaNs;

        // Procesamos el bloque residual
        FrameData frame = processBlock(m_accumBuffer, blockTsNs, m_totalSamples);

        // Guardamos en la base de datos
        saveFrameToDb(frame, m_blockIndex);

        // Emitimos como batch de un solo frame
        QVector<FrameData> batch;
        batch.append(frame);
        emit framesReady(batch);

        m_totalSamples += m_accumBuffer.size();
        ++m_blockIndex;
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
    m_startTimestampNs = -1;

    // Reinicializar calculador de espectrograma
    initializeSpectrogramCalculator();

    emit statsUpdated(0, 0, 0);
}

FrameData DSPWorker::processBlock(const QVector<float>& block,
                                  quint64 timestamp,
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
            m_db->insertBlock(m_blockIndex,
                              frame.sampleOffset,
                              blob,
                              frame.timestamp);
        }

    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error procesando bloque: %1").arg(e.what()));
    }

    return frame;
}

void DSPWorker::saveFrameToDb(const FrameData& frame, qint64 blockIndex) {
    if (!m_db) return;

    try {
        // Verificar timestamp antes de guardar
        if (frame.timestamp == 0 || frame.timestamp == static_cast<quint64>(-1)) {
            qWarning() << "DSPWorker: timestamp inválido en bloque" << blockIndex
                       << ":" << frame.timestamp;
        }

        // Guardar picos si están habilitados
        if (m_cfg.enablePeaks && frame.waveform.size() >= 2) {
            m_db->insertPeak(blockIndex, frame.sampleOffset, frame.waveform[0], frame.waveform[1], frame.timestamp);
        }

        // Nota: El bloque raw ya se guardó en processBlock()
        // para mantener el orden correcto de las operaciones

    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error guardando en DB: %1").arg(e.what()));
    }
}

quint64 DSPWorker::validateTimestamp(quint64 timestampNs) {
    // Verificar si el timestamp es válido
    if (timestampNs == 0 || timestampNs == static_cast<quint64>(-1)) {
        qWarning() << "DSPWorker: timestamp inválido recibido:" << timestampNs
                   << "- usando timestamp actual";
        return getCurrentTimestampNs();
    }

    // Verificar si el timestamp está en un rango razonable
    quint64 currentNs = getCurrentTimestampNs();
    quint64 maxDiff = 3600ULL * 1000000000ULL; // 1 hora en nanosegundos

    if (timestampNs > currentNs + maxDiff || timestampNs < currentNs - maxDiff) {
        qWarning() << "DSPWorker: timestamp fuera de rango:" << timestampNs
                   << "vs actual:" << currentNs << "- usando timestamp actual";
        return currentNs;
    }

    return timestampNs;
}

quint64 DSPWorker::getCurrentTimestampNs() {
    return static_cast<quint64>(QDateTime::currentMSecsSinceEpoch()) * 1000000ULL;
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
