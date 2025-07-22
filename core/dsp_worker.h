#ifndef DSP_WORKER_H
#define DSP_WORKER_H

#include "config/audio_configs.h"
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
    quint64 timestamp;              ///< Timestamp en nanosegundos
    qint64 sampleOffset;            ///< Offset de muestra desde el inicio
    QVector<float> waveform;        ///< Datos de forma de onda
    QVector<float> spectrum;        ///< Espectro de frecuencias
    QVector<float> frequencies;     ///< Frecuencias correspondientes a cada bin
    float windowGain = 1.0f;        ///< Ganancia de la ventana aplicada
};

/**
 * @brief Worker para procesamiento DSP de audio en tiempo real
 *
 * Procesa chunks de audio, calcula picos, espectrograma y
 * almacena resultados en base de datos.
 *
 * Todos los timestamps se manejan en nanosegundos para máxima precisión.
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
    /** Procesa un chunk de muestras de audio con timestamp en nanosegundos */
    void processChunk(const QVector<float>& samples, quint64 timestampNs);

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
    /** Procesa un bloque individual de muestras con timestamp en nanosegundos */
    FrameData processBlock(const QVector<float>& block, quint64 timestampNs, qint64 sampleOffset);

    /** Guarda un frame en la base de datos */
    void saveFrameToDb(const FrameData& frame, qint64 blockIndex);

    /** Inicializa el calculador de espectrograma */
    void initializeSpectrogramCalculator();

    /** Actualiza la configuración del calculador de espectrograma */
    void updateSpectrogramConfig();

    /** Valida y corrige timestamps inválidos */
    quint64 validateTimestamp(quint64 timestampNs);

    /** Obtiene el timestamp actual en nanosegundos */
    quint64 getCurrentTimestampNs();

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
    qint64 m_startTimestampNs = 0;     ///< Timestamp de inicio en nanosegundos
    qint64 m_totalSamples = 0;          ///< Total de muestras procesadas
    qint64 m_blockIndex = 0;            ///< Índice de bloque

    // Calculador de espectrograma
    std::unique_ptr<SpectrogramCalculator> m_spectrogramCalc;

    // Métodos legacy (mantenidos para compatibilidad)
    QVector<float> m_hanningWindow;     ///< Ventana de Hanning (legacy)
    bool m_windowCalculated = false;    ///< Flag ventana calculada (legacy)
};

#endif // DSP_WORKER_H
