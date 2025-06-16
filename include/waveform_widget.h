#ifndef WAVEFORM_WIDGET_H
#define WAVEFORM_WIDGET_H
#include <QWidget>
#include <QVector>
#include <QColor>
#include <QAudioFormat>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QFont>

class QPaintEvent;
class QMouseEvent;
class QWheelEvent;

/**
 * @brief Widget para visualización de forma de onda de audio en tiempo real
 *
 * Este widget proporciona una visualización configurable de datos de audio,
 * incluyendo diferentes modos de renderizado, escalado automático,
 * marcadores de pico y líneas RMS.
 */
class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Modos de renderizado de la forma de onda
     */
    enum class RenderMode {
        Line,   ///< Línea continua
        Filled, ///< Área rellena
        Bars    ///< Barras verticales
    };

    /**
     * @brief Constructor
     * @param parent Widget padre
     */
    explicit WaveformWidget(QWidget *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~WaveformWidget() override;

    // Métodos de configuración
    void setDisplayDuration(double seconds);
    void setAudioFormat(const QAudioFormat& format);
    void setAmplitudeScale(double scale);
    void setVerticalOffset(double offset);
    void setAutoScale(bool enabled);
    void setShowGrid(bool show);
    void setShowPeakMarkers(bool show);
    void setShowRMSLine(bool show);
    void setShowZeroLine(bool show);
    void setWaveformColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    void setRenderMode(RenderMode mode);
    void setSmoothing(bool enabled);
    void setFrameRate(double fps);

    // Getters
    double displayDuration() const { return m_displayDuration; }
    double amplitudeScale() const { return m_amplitudeScale; }
    double verticalOffset() const { return m_verticalOffset; }
    bool autoScale() const { return m_autoScale; }
    bool showGrid() const { return m_showGrid; }
    bool showPeakMarkers() const { return m_showPeakMarkers; }
    bool showRMSLine() const { return m_showRMSLine; }
    bool showZeroLine() const { return m_showZeroLine; }
    QColor waveformColor() const { return m_waveformColor; }
    QColor backgroundColor() const { return m_backgroundColor; }
    RenderMode renderMode() const { return m_renderMode; }
    bool smoothing() const { return m_smoothing; }
    double frameRate() const { return m_frameRate; }
    double peakLevel() const { return m_peakLevel; }
    double rmsLevel() const { return m_rmsLevel; }

public slots:
    /**
     * @brief Actualizar datos de forma de onda
     * @param waveform Datos de la forma de onda
     */
    void updateWaveform(const QVector<float>& waveform);

    /**
     * @brief Actualizar datos de forma de onda con niveles
     * @param waveform Datos de la forma de onda
     * @param peakLevel Nivel de pico
     * @param rmsLevel Nivel RMS
     */
    void updateWaveform(const QVector<float>& waveform, double peakLevel, double rmsLevel);

    /**
     * @brief Limpiar visualización
     */
    void clearWaveform();

signals:
    /**
     * @brief Señal emitida cuando se hace click en una posición específica
     * @param timeSeconds Tiempo en segundos
     * @param amplitude Amplitud en esa posición
     */
    void positionClicked(double timeSeconds, float amplitude);

protected:
    // Eventos Qt
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void showCursorInfo(const QPoint& pos);

private:
    // Métodos de dibujo
    void drawGrid(QPainter& painter);
    void drawZeroLine(QPainter& painter);
    void drawWaveform(QPainter& painter);
    void drawWaveformLine(QPainter& painter, int width, int height, int centerY);
    void drawWaveformFilled(QPainter& painter, int width, int height, int centerY);
    void drawWaveformBars(QPainter& painter, int width, int height, int centerY);
    void drawRMSLine(QPainter& painter);
    void drawPeakMarkers(QPainter& painter);
    void drawStatusInfo(QPainter& painter);

    // Métodos de cálculo
    void calculateStatistics();
    void updateAutoScale();
    QVector<int> findPeaks() const;

    // Constantes
    static const double DEFAULT_DISPLAY_DURATION;
    static const int DEFAULT_GRID_DIVISIONS;
    static const QColor DEFAULT_WAVEFORM_COLOR;
    static const QColor DEFAULT_BACKGROUND_COLOR;
    static const QColor DEFAULT_GRID_COLOR;

    // Configuración de visualización
    double m_displayDuration;
    int m_sampleRate;
    int m_channels;
    bool m_autoScale;
    bool m_showGrid;
    bool m_showPeakMarkers;
    bool m_showRMSLine;
    bool m_showZeroLine;
    double m_amplitudeScale;
    double m_verticalOffset;
    int m_gridDivisions;

    // Colores
    QColor m_waveformColor;
    QColor m_backgroundColor;
    QColor m_gridColor;
    QColor m_peakMarkerColor;
    QColor m_rmsLineColor;
    QColor m_zeroLineColor;
    QColor m_textColor;

    // Configuración de renderizado
    RenderMode m_renderMode;
    bool m_smoothing;
    QFont m_font;

    // Datos de la forma de onda
    QVector<float> m_waveformData;

    // Estadísticas
    double m_peakLevel;
    double m_rmsLevel;

    // Control de frame rate y actualización
    QTimer* m_updateTimer;
    qint64 m_lastUpdateTime;
    double m_frameRate;
    double m_maxFrameTime;

    // Mutex para thread safety
    QMutex m_dataMutex;

    // Mouse tracking
    bool m_isMouseTracking;
    QPoint m_lastMousePos;
};

#endif // WAVEFORM_WIDGET_H
