#ifndef DSP_WORKER_H
#define DSP_WORKER_H

#include <QObject>
#include <QVector>
#include <QtTypes>

// Forward declaration
class AudioDb;

/**
 * @brief Configuración para el procesamiento DSP
 */
struct DSPConfig {
    int blockSize = 1024;       ///< Tamaño del bloque para procesamiento
    int fftSize = 1024;         ///< Tamaño de la FFT para espectrograma
    int sampleRate = 44100;     ///< Frecuencia de muestreo
    bool enableSpectrum = true; ///< Habilitar cálculo de espectro
    bool enablePeaks = true;    ///< Habilitar detección de picos

    // Constructor por defecto
    DSPConfig() = default;

    // Constructor con parámetros
    DSPConfig(int blockSz, int fftSz, int sampleRt = 44100)
        : blockSize(blockSz)
        , fftSize(fftSz)
        , sampleRate(sampleRt)
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

public slots:
    /** Procesa un chunk de muestras de audio */
    void processChunk(const QVector<float>& samples, qint64 timestamp);

    /** Procesa las muestras residuales al finalizar */
    void flushResidual();

    /** Reinicia el estado interno del worker */
    void reset();

signals:
    /** Emitido cuando se calculan los valores min/max de un bloque */
    void minMaxReady(float min, float max, qint64 timestamp);

    /** Emitido cuando está lista una columna del espectrograma */
    void specColumnReady(const QVector<float>& magnitudes, qint64 timestamp);

    /** Emitido al producirse un error */
    void errorOccurred(const QString& error);

    /** Emitido periódicamente con estadísticas de procesamiento */
    void statsUpdated(qint64 blocksProcessed, qint64 samplesProcessed, int bufferSize);

private:
    void handleBlock(const QVector<float>& block, qint64 timestamp);
    void calculateSpectrum(const QVector<float>& block, qint64 timestamp);
    QVector<float> calculateHanningWindow(int size) const;
    QVector<float> applyWindow(const QVector<float>& samples,
                               const QVector<float>& window) const;

    DSPConfig      m_cfg;             ///< Configuración DSP
    AudioDb*       m_db;              ///< Puntero a la base de datos
    QVector<float> m_accumBuffer;     ///< Buffer de acumulación
    qint64         m_totalSamples = 0;///< Total de muestras procesadas
    qint64         m_blockIndex = 0;  ///< Índice de bloque

    QVector<float> m_hanningWindow;   ///< Ventana de Hanning
    bool           m_windowCalculated = false;
};

#endif // DSP_WORKER_H
