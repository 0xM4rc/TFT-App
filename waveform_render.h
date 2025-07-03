#ifndef WAVEFORM_RENDER_H
#define WAVEFORM_RENDER_H

#include "dsp_worker.h"
#include <QWidget>
#include <QVector>
#include <QPainter>
#include <QTimer>
#include <QMutex>
#include <QtTypes>

/**
 * @brief Estructura para representar un bloque de waveform
 */
struct WaveformBlock {
    qint64 blockIndex;          ///< Índice del bloque
    qint64 timestamp;           ///< Timestamp del bloque
    qint64 sampleOffset;        ///< Offset de muestra
    QVector<float> samples;     ///< Muestras del bloque
    float minValue;             ///< Valor mínimo del bloque
    float maxValue;             ///< Valor máximo del bloque
    float rmsValue;             ///< Valor RMS del bloque

    WaveformBlock()
        : blockIndex(0), timestamp(0), sampleOffset(0)
        , minValue(0.0f), maxValue(0.0f), rmsValue(0.0f) {}
};

/**
 * @brief Configuración para el renderizado del waveform
 */
struct WaveformConfig {
    int maxVisibleBlocks = 100;     ///< Máximo de bloques visibles
    int blockWidth = 4;             ///< Ancho de cada bloque en pixels
    int blockSpacing = 1;           ///< Espaciado entre bloques
    QColor peakColor = Qt::green;   ///< Color de los picos
    QColor rmsColor = Qt::blue;     ///< Color del RMS
    QColor backgroundColor = Qt::black; ///< Color de fondo
    bool showPeaks = true;          ///< Mostrar picos
    bool showRMS = true;            ///< Mostrar RMS
    bool autoScale = true;          ///< Escalado automático
    float manualScale = 1.0f;       ///< Escala manual cuando autoScale = false
    bool scrolling = true;          ///< Desplazamiento automático
    int updateInterval = 50;        ///< Intervalo de actualización en ms
};

/**
 * @brief Widget para renderizar waveform por bloques
 */
class WaveformRenderer : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformRenderer(QWidget* parent = nullptr);
    ~WaveformRenderer();

    /** Configurar el renderizador */
    void setConfig(const WaveformConfig& config);
    WaveformConfig getConfig() const;

    /** Obtener estadísticas */
    int getBlockCount() const;
    int getVisibleBlocks() const;
    qint64 getLatestTimestamp() const;

public slots:
    /** Procesar frames del DSPWorker */
    void processFrames(const QVector<FrameData>& frames);

    /** Limpiar el waveform */
    void clear();

    /** Pausar/reanudar la actualización */
    void setPaused(bool paused);

    /** Configurar zoom */
    void setZoom(float zoom);

signals:
    /** Emitido cuando se actualiza el waveform */
    void waveformUpdated(int blockCount);

    /** Emitido cuando se hace clic en un bloque */
    void blockClicked(qint64 blockIndex, qint64 timestamp);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void updateDisplay();

private:
    void addBlock(const WaveformBlock& block);
    void calculateBlockStats(WaveformBlock& block);
    void drawBlock(QPainter& painter, const WaveformBlock& block, int x, int height);
    void drawPeaks(QPainter& painter, const WaveformBlock& block, int x, int height);
    void drawRMS(QPainter& painter, const WaveformBlock& block, int x, int height);
    void updateVisibleRange();
    float scaleValue(float value, int height) const;
    int getBlockAtPosition(int x) const;

    WaveformConfig m_config;
    QVector<WaveformBlock> m_blocks;
    mutable QMutex m_mutex;
    QTimer* m_updateTimer;

    // Estado de renderizado
    float m_maxAmplitude;
    float m_zoom;
    int m_scrollOffset;
    bool m_paused;
    bool m_needsUpdate;

    // Rango visible
    int m_visibleStartIndex;
    int m_visibleEndIndex;

    // Métricas
    qint64 m_totalBlocks;
    qint64 m_latestTimestamp;
};

#endif // WAVEFORM_RENDER_H
