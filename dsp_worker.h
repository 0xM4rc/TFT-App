#ifndef DSP_WORKER_H
#define DSP_WORKER_H

#include <QObject>
#include <QVector>
#include <QtTypes>
#include <memory>

// Forward declarations
class AudioDb;
class SpectrogramCalculator;

/**
 * @brief Datos de un frame procesado
 */
struct FrameData {
    qint64 timestamp;
    qint64 sampleOffset;
    QVector<float> waveform;
    QVector<float> spectrum;
    QVector<float> frequencies;  // Añadido para frecuencias del espectrograma
    float windowGain = 1.0f;     // Añadido para ganancia de ventana
};

/**
 * @brief Configuración para el procesamiento DSP
 */
struct DSPConfig {
    int blockSize = 1024;       ///< Tamaño del bloque para procesamiento
    int fftSize = 1024;         ///< Tamaño de la FFT para espectrograma
    int sampleRate = 44100;     ///< Frecuencia de muestreo
    bool enableSpectrum = true; ///< Habilitar cálculo de espectro
    bool enablePeaks = true;    ///< Habilitar detección de picos
    int waveformSize = 512;     ///< Tamaño del waveform para picos

    // Configuración específica para espectrograma
    int hopSize = 512;          ///< Tamaño del salto entre ventanas
    int windowType = 1;         ///< Tipo de ventana (0=Rectangular, 1=Hann, etc.)
    double kaiserBeta = 8.0;    ///< Parámetro beta para ventana Kaiser
    double gaussianSigma = 0.4; ///< Parámetro sigma para ventana Gaussiana
    bool logScale = true;       ///< Aplicar escala logarítmica (dB)
    float noiseFloor = -100.0f; ///< Piso de ruido en dB

    // Constructor por defecto
    DSPConfig() = default;

    // Constructor con parámetros básicos
    DSPConfig(int blockSz, int fftSz, int sampleRt = 44100)
        : blockSize(blockSz)
        , fftSize(fftSz)
        , sampleRate(sampleRt)
        , hopSize(fftSz / 2)  // Hop size típico es la mitad del FFT size
    {}
};

/**
 * @brief Worker para procesamiento DSP de audio en tiempo real
 *
 * Procesa chunks de audio, calcula picos, espectrograma y
 * almacena resultados en base de datos.
 */
class DSPWorker : public QObject
{
    Q_OBJECT

public:
    explicit DSPWorker(const DSPConfig& cfg, AudioDb* db, QObject* parent = nullptr);
    ~DSPWorker();

    /** Obtiene la configuración actual */
    DSPConfig getConfig() const;

    /** Actualiza la configuración DSP */
    void setConfig(const DSPConfig& cfg);

    /** Obtiene el número total de muestras procesadas */
    qint64 getTotalSamples() const;

    /** Obtiene el índice del bloque actual */
    qint64 getBlockIndex() const;

    /** Obtiene el tamaño del buffer de acumulación actual */
    int getAccumBufferSize() const;

    /** Obtiene las frecuencias correspondientes a cada bin de la FFT */
    QVector<float> getFrequencyBins() const;

    /** Obtiene información del estado actual como string */
    QString getStatusInfo() const;

    /** Obtiene información sobre la configuración del espectrograma */
    QString getSpectrogramInfo() const;

public slots:
    /** Procesa un chunk de muestras de audio */
    void processChunk(const QVector<float>& samples, qint64 timestamp);

    /** Procesa las muestras residuales al finalizar */
    void flushResidual();

    /** Reinicia el estado interno del worker */
    void reset();

signals:
    /** Emitido cuando hay nuevos frames listos */
    void framesReady(const QVector<FrameData>& batch);

    /** Emitido al producirse un error */
    void errorOccurred(const QString& error);

    /** Emitido periódicamente con estadísticas de procesamiento */
    void statsUpdated(qint64 blocksProcessed, qint64 samplesProcessed, int bufferSize);

private:
    /** Procesa un bloque individual de muestras */
    FrameData processBlock(const QVector<float>& block, qint64 timestamp, qint64 sampleOffset);

    /** Guarda un frame en la base de datos */
    void saveFrameToDb(const FrameData& frame, qint64 blockIndex);

    /** Inicializa el calculador de espectrograma */
    void initializeSpectrogramCalculator();

    /** Actualiza la configuración del calculador de espectrograma */
    void updateSpectrogramConfig();

    // Métodos legacy mantenidos para compatibilidad
    void handleBlock(const QVector<float>& block, qint64 timestamp);
    void calculateSpectrum(const QVector<float>& block, qint64 timestamp);
    QVector<float> calculateHanningWindow(int size) const;
    QVector<float> applyWindow(const QVector<float>& samples,
                               const QVector<float>& window) const;
    QVector<float> calculateSpectrum(const QVector<float>& block);

private:
    DSPConfig m_cfg;                    ///< Configuración DSP
    AudioDb* m_db;                      ///< Puntero a la base de datos
    QVector<float> m_accumBuffer;       ///< Buffer de acumulación
    qint64 m_startTimestamp = -1;       ///< Timestamp de inicio
    qint64 m_totalSamples = 0;          ///< Total de muestras procesadas
    qint64 m_blockIndex = 0;            ///< Índice de bloque

    // Calculador de espectrograma
    std::unique_ptr<SpectrogramCalculator> m_spectrogramCalc;

    // Métodos legacy (mantenidos para compatibilidad)
    QVector<float> m_hanningWindow;     ///< Ventana de Hanning (legacy)
    bool m_windowCalculated = false;    ///< Flag ventana calculada (legacy)
};

#endif // DSP_WORKER_H
