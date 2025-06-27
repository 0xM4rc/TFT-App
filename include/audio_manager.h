#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <QObject>
#include <QTimer>
#include <QByteArray>
#include <QMutex>
#include <QAudioFormat>
#include <QThread>
#include <QAudioDevice>
#include <QUrl>
#include <atomic>
#include <memory>

#include "include/audio_processor.h"
#include "include/data_structures/audio_configuration.h"
#include "include/data_structures/visualization_data.h"
#include "include/data_structures/audio_statistics.h"
#include "source_controller.h"

/**
 * @brief AudioManager mejorado para procesamiento de audio en tiempo real
 *
 * Esta clase gestiona el procesamiento de audio en tiempo real utilizando
 * un AudioProcessor optimizado y un SourceController para múltiples fuentes.
 *
 * Características principales:
 * - Procesamiento en hilo separado de alta prioridad
 * - Buffers lock-free para menor latencia
 * - Integración completa con AudioProcessor
 * - Estadísticas de rendimiento en tiempo real
 * - Soporte para múltiples fuentes de audio
 */
class AudioManager : public QObject
{
    Q_OBJECT

public:
    explicit AudioManager(QObject* parent = nullptr);
    ~AudioManager();

    // ===============================================
    // CONTROL PRINCIPAL
    // ===============================================

    /**
     * @brief Inicia el procesamiento de audio
     *
     * Crea un hilo de procesamiento dedicado, inicializa el AudioProcessor
     * y comienza la captura desde la fuente activa.
     */
    void startProcessing();

    /**
     * @brief Detiene el procesamiento de audio
     *
     * Detiene todos los componentes y limpia los recursos.
     */
    void stopProcessing();

    /**
     * @brief Indica si está procesando audio actualmente
     */
    bool isProcessing() const;

    // ===============================================
    // CONFIGURACIÓN
    // ===============================================

    /**
     * @brief Establece la tasa de actualización en milisegundos
     * @param ms Tasa entre 5-100ms (para tiempo real)
     */
    void setUpdateRate(int ms);
    int getUpdateRate() const;

    /**
     * @brief Establece la duración del waveform en segundos
     * @param seconds Duración entre 0.001-1.0s
     */
    void setWaveformDuration(double seconds);

    /**
     * @brief Establece el número de muestras para el waveform
     * @param samples Número entre 64-4096
     */
    void setWaveformSamples(int samples);

    /**
     * @brief Aplica una configuración completa de audio
     * @param config Configuración validada
     */
    void applyConfiguration(const AudioConfiguration& config);

    // ===============================================
    // GESTIÓN DE FUENTES
    // ===============================================

    /**
     * @brief Configura y activa una fuente de red (stream/URL)
     * @param input_url URL del stream de audio
     * @return true si se configuró correctamente
     */
    bool fetchURL(const QUrl& input_url);

    /**
     * @brief Configura un micrófono
     * @param micKey Identificador del micrófono
     * @param newDevice Dispositivo de audio
     * @return true si se configuró correctamente
     */
    bool fetchMicrophone(const QString& micKey, const QAudioDevice& newDevice);

    // ===============================================
    // ESTADÍSTICAS Y MONITOREO
    // ===============================================

    /**
     * @brief Obtiene la carga de procesamiento actual (%)
     */
    double getProcessingLoad() const;

    /**
     * @brief Obtiene el total de muestras procesadas
     */
    qint64 getTotalSamplesProcessed() const;

    /**
     * @brief Obtiene la latencia promedio en milisegundos
     */
    double getAverageLatency() const;

    /**
     * @brief Obtiene el formato de audio actual
     */
    QAudioFormat getCurrentFormat() const;

    /**
     * @brief Obtiene estadísticas completas
     */
    AudioStatistics getStatistics() const;

signals:
    // ===============================================
    // SEÑALES DE CONTROL
    // ===============================================

    /**
     * @brief Se emite cuando comienza el procesamiento
     */
    void processingStarted();

    /**
     * @brief Se emite cuando se detiene el procesamiento
     */
    void processingStopped();

    /**
     * @brief Se emite cuando hay un error de procesamiento
     */
    void processingError(const QString& error);

    // ===============================================
    // SEÑALES DE DATOS
    // ===============================================

    /**
     * @brief Se emite cuando hay nuevos datos de visualización disponibles
     * @param data Datos de visualización completos (waveform, spectrum, etc.)
     */
    void visualizationDataReady(const VisualizationData& data);

    /**
     * @brief Se emite periódicamente con estadísticas actualizadas
     */
    void statisticsUpdated(double loadPercent, qint64 totalSamples, double avgLatencyMs);

    /**
     * @brief Se emite cuando se detecta un nuevo formato de audio
     */
    void formatChanged(const QAudioFormat& format);

    /**
     * @brief Se emite cuando cambia el estado de la fuente
     */
    void sourceStateChanged(bool active);

public slots:
    // ===============================================
    // SLOTS PARA DATOS DE FUENTES
    // ===============================================

    /**
     * @brief Maneja datos crudos de audio desde las fuentes
     * @param type Tipo de fuente
     * @param id Identificador de la fuente
     * @param rawData Datos de audio crudos
     */
    void handleRawData(SourceType type, const QString& id, const QByteArray& rawData);

    /**
     * @brief Maneja la detección de formato de audio
     */
    void onFormatDetected(SourceType type, const QString& id, const QAudioFormat& format);

    /**
     * @brief Maneja cambios de estado de las fuentes
     */
    void onSourceStateChanged(SourceType type, const QString& id, bool active);

private slots:
    // ===============================================
    // SLOTS INTERNOS
    // ===============================================

    /**
     * @brief Procesa datos acumulados (llamado por timer)
     */
    void processAccumulatedData();

    /**
     * @brief Maneja errores del procesador
     */
    void onProcessorError(const QString& error);

    // /**
    //  * @brief Maneja advertencias de rendimiento
    //  */
    // void onPerformanceWarning(const QString& warning);

    // /**
    //  * @brief Maneja datos de visualización listos del procesador
    //  */
    // void onVisualizationDataReady(const VisualizationData& data);

private:
    // ===============================================
    // COMPONENTES PRINCIPALES
    // ===============================================

    /// Configuración de audio actual
    AudioConfiguration m_audioConfig;

    /// Procesador de audio optimizado para tiempo real
    std::unique_ptr<AudioProcessor> m_processor;

    /// Controlador de fuentes de audio
    SourceController* m_controller;

    /// Timer para procesamiento periódico
    QTimer m_processingTimer;

    /// Hilo dedicado para procesamiento
    QThread* m_processingThread;

    // ===============================================
    // ESTADO Y CONFIGURACIÓN
    // ===============================================

    /// Indica si está procesando
    std::atomic<bool> m_isProcessing{false};

    /// Formato de audio actual
    QAudioFormat m_currentFormat;

    /// Indica si el formato es válido
    std::atomic<bool> m_formatValid{false};

    /// Tasa de actualización en ms
    int m_updateRateMs;

    /// Duración del waveform en segundos
    double m_waveformDurationS;

    /// Número de muestras para waveform
    int m_waveformSamples;

    /// Tamaño máximo del buffer
    int m_maxBufferSize;

    // ===============================================
    // IDENTIFICADORES DE FUENTES
    // ===============================================

    /// ID de la fuente de red
    QString network_source_id;

    /// ID de la fuente de micrófono
    QString mic_source_id;

    // ===============================================
    // ESTADÍSTICAS Y MONITOREO
    // ===============================================

    /// Tiempo total de procesamiento en microsegundos
    std::atomic<qint64> m_totalProcTimeUs{0};

    /// Total de muestras procesadas
    std::atomic<qint64> m_totalSamples{0};

    /// Contador de chunks procesados
    std::atomic<int> m_chunksCount{0};

    /// Latencia promedio en milisegundos
    std::atomic<double> m_avgLatencyMs{0.0};

    // ===============================================
    // DATOS Y SINCRONIZACIÓN
    // ===============================================

    /// Buffer de datos pendientes para procesar
    QByteArray m_pendingData;

    /// Mutex para proteger el buffer de datos
    QMutex m_dataMutex;

    // ===============================================
    // MÉTODOS PRIVADOS
    // ===============================================

    /**
     * @brief Procesa un chunk individual de audio
     * @param data Datos de audio en formato raw
     * @return Datos de visualización generados
     */
    VisualizationData processAudioChunk(const QByteArray& data);

    /**
     * @brief Convierte datos raw a muestras float
     * @param data Datos raw de audio
     * @param format Formato del audio
     * @return Vector de muestras normalizadas [-1.0, 1.0]
     */
    QVector<float> convertToFloat(const QByteArray& data, const QAudioFormat& format);

    /**
     * @brief Calcula el espectro de frecuencias
     * @param samples Muestras de audio
     * @return Magnitudes del espectro
     */
    QVector<float> calculateSpectrum(const QVector<float>& samples);

    /**
     * @brief Calcula niveles de pico y RMS
     * @param samples Muestras de audio
     * @param peak Nivel de pico calculado
     * @param rms Nivel RMS calculado
     */
    void calculateLevels(const QVector<float>& samples, double& peak, double& rms);

    /**
     * @brief Reinicia las estadísticas de procesamiento
     */
    void resetStatistics();

    /**
     * @brief Actualiza las estadísticas con nuevos datos
     * @param processingTimeUs Tiempo de procesamiento en microsegundos
     * @param samplesProcessed Número de muestras procesadas
     */
    void updateStatistics(qint64 processingTimeUs, int samplesProcessed);
};

#endif // AUDIO_MANAGER_H
