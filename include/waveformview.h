#ifndef WAVEFORMVIEW_H
#define WAVEFORMVIEW_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QVector>
#include <QColor>
#include "include/data_structures/file_audio_data.h"

/**
 * @brief Datos pre-procesados para renderizado eficiente de forma de onda
 */
struct WaveformData {
    QVector<float> minValues;  ///< Valores mínimos por bloque
    QVector<float> maxValues;  ///< Valores máximos por bloque
    int samplesPerBlock = 1;   ///< Número de muestras representadas por cada bloque
    int originalSampleCount = 0; ///< Número total de muestras originales

    void clear() {
        minValues.clear();
        maxValues.clear();
        samplesPerBlock = 1;
        originalSampleCount = 0;
    }

    bool isEmpty() const {
        return minValues.isEmpty();
    }

    int blockCount() const {
        return minValues.size();
    }
};

/**
 * @brief Widget para visualizar formas de onda de audio
 *
 * Características:
 * - Visualización mono o multicanal
 * - Zoom horizontal y vertical
 * - Downsampling automático para rendimiento
 * - Selección de regiones
 * - Cursor de reproducción
 */
class WaveformView : public QWidget {
    Q_OBJECT

public:
    enum DisplayMode {
        Mono,           ///< Mostrar como mono (mezcla de canales)
        Stereo,         ///< Mostrar canales L/R separados
        AllChannels     ///< Mostrar todos los canales por separado
    };

    enum RenderStyle {
        Line,           ///< Líneas simples conectando puntos
        Filled,         ///< Áreas rellenas desde el centro
        Outline         ///< Solo contornos min/max
    };

    explicit WaveformView(QWidget *parent = nullptr);

    // Configuración de datos
    void setAudioData(const FileAudioData &audio);
    void clearAudioData();

    // Propiedades de visualización
    void setDisplayMode(DisplayMode mode);
    DisplayMode displayMode() const { return m_displayMode; }

    void setRenderStyle(RenderStyle style);
    RenderStyle renderStyle() const { return m_renderStyle; }

    // Colores
    void setWaveformColor(const QColor &color);
    void setChannelColor(int channel, const QColor &color);
    void setBackgroundColor(const QColor &color);
    void setGridColor(const QColor &color);

    QColor waveformColor() const { return m_waveformColor; }
    QColor backgroundColor() const { return m_backgroundColor; }

    // Zoom y navegación
    void setZoomLevel(double zoom);
    double zoomLevel() const { return m_zoomLevel; }

    void setScrollPosition(double position); // 0.0 - 1.0
    double scrollPosition() const { return m_scrollPosition; }

    // Cursor de reproducción
    void setPlaybackPosition(double position); // 0.0 - 1.0
    double playbackPosition() const { return m_playbackPosition; }
    void setShowPlaybackCursor(bool show);

    // Selección
    void setSelection(double start, double end); // 0.0 - 1.0
    void clearSelection();
    bool hasSelection() const { return m_hasSelection; }
    QPair<double, double> selection() const;

    // Grid y marcadores
    void setShowGrid(bool show);
    void setShowTimeMarkers(bool show);
    void setShowAmplitudeMarkers(bool show);

    // Información del audio
    bool hasAudioData() const { return !m_audioData.isEmpty(); }
    double audioDurationSeconds() const;
    int audioSampleRate() const;

    // Utilidades de conversión
    double pixelToTime(int pixel) const;
    int timeToPixel(double time) const;
    double pixelToAmplitude(int pixel, int channelHeight) const;
    int amplitudeToPixel(double amplitude, int channelHeight) const;

signals:
    void selectionChanged(double start, double end);
    void playbackPositionClicked(double position);
    void zoomChanged(double newZoom);
    void scrollChanged(double newPosition);

protected:
    // Eventos Qt
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void updateWaveformData();

private:
    // Métodos de renderizado
    void drawBackground(QPainter &painter);
    void drawGrid(QPainter &painter);
    void drawWaveform(QPainter &painter);
    void drawChannel(QPainter &painter, int channelIndex, const QRect &channelRect);
    void drawSelection(QPainter &painter);
    void drawPlaybackCursor(QPainter &painter);
    void drawTimeMarkers(QPainter &painter);
    void drawAmplitudeMarkers(QPainter &painter, const QRect &channelRect);

    // Métodos auxiliares
    void calculateWaveformData();
    WaveformData downsampleChannel(const QVector<float> &samples, int targetBlocks) const;
    QRect getChannelRect(int channelIndex) const;
    int getVisibleChannelCount() const;
    void updateScrollBounds();
    void clampScrollPosition();

    // Manejo de mouse
    void handleMousePress(QMouseEvent *event);
    void handleMouseMove(QMouseEvent *event);
    void handleMouseRelease(QMouseEvent *event);

    // Datos de audio
    FileAudioData m_audioData;
    QVector<WaveformData> m_waveformData; // Un WaveformData por canal
    WaveformData m_monoWaveformData;      // Datos pre-calculados para modo mono

    // Configuración de visualización
    DisplayMode m_displayMode;
    RenderStyle m_renderStyle;

    // Colores
    QColor m_waveformColor;
    QColor m_backgroundColor;
    QColor m_gridColor;
    QColor m_selectionColor;
    QColor m_playbackCursorColor;
    QVector<QColor> m_channelColors; // Colores específicos por canal

    // Zoom y navegación
    double m_zoomLevel;        // Factor de zoom (1.0 = sin zoom)
    double m_scrollPosition;   // Posición de scroll (0.0 - 1.0)
    double m_maxScrollPosition;

    // Cursor de reproducción
    double m_playbackPosition;
    bool m_showPlaybackCursor;

    // Selección
    bool m_hasSelection;
    double m_selectionStart;
    double m_selectionEnd;

    // Grid y marcadores
    bool m_showGrid;
    bool m_showTimeMarkers;
    bool m_showAmplitudeMarkers;

    // Interacción con mouse
    enum MouseMode {
        None,
        Selecting,
        Dragging,
        Zooming
    };

    MouseMode m_mouseMode;
    QPoint m_mouseStartPos;
    double m_selectionStartTime;
    double m_initialScrollPosition;

    // Optimización
    QTimer *m_updateTimer;
    bool m_needsDataUpdate;
    int m_lastWidth;

    // Configuración de renderizado
    static constexpr int MIN_PIXEL_PER_SAMPLE = 1;
    static constexpr int MAX_BLOCKS_PER_PIXEL = 4;
    static constexpr int CHANNEL_MARGIN = 2;
    static constexpr int TIME_MARKER_HEIGHT = 20;
    static constexpr int AMPLITUDE_MARKER_WIDTH = 40;
};
#endif // WAVEFORMVIEW_H
