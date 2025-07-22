#ifndef SPECTROGRAM_RENDERER_H
#define SPECTROGRAM_RENDERER_H

#include <QWidget>
#include <QVector>
#include <QMutex>
#include <QTimer>
#include <QImage>
#include <QRect>
#include <memory>
#include "core/dsp_worker.h"

struct SpectrogramConfig {
    int    fftSize        = 1024;      // debe coincidir con DSPConfig.fftSize
    int    sampleRate     = 44100;
    int    blockWidth     = 2;         // ancho en píxeles por columna
    int    updateInterval = 30;        // ms entre repintados
    int    maxColumns     = 500;       // número máximo de columnas (tiempo)
    bool   autoScroll     = true;      // desplazamiento automático
    float  minDb          = -100.0f;   // piso en dB (negro)
    float  maxDb          =   0.0f;    // tope en dB (blanco)

    // Validación de configuración
    bool isValid() const {
        return fftSize > 0 && sampleRate > 0 && blockWidth > 0 &&
               updateInterval > 0 && maxColumns > 0 && minDb < maxDb;
    }
};

enum class ColorMapType {
    Roesus,     // Original rosa-magenta-amarillo
    Viridis,    // Azul-verde-amarillo
    Plasma,     // Azul-magenta-amarillo
    Grayscale   // Escala de grises
};

class SpectrogramRenderer : public QWidget {
    Q_OBJECT

public:
    explicit SpectrogramRenderer(QWidget* parent = nullptr);
    ~SpectrogramRenderer() override;

    void setConfig(const SpectrogramConfig& cfg);
    SpectrogramConfig config() const;

    void setColorMap(ColorMapType type);
    ColorMapType colorMapType() const { return m_colorMapType; }

    // Métodos para control manual del scroll
    void setScrollPosition(double position); // 0.0 a 1.0
    double scrollPosition() const;

    // Información de estado
    int columnCount() const;
    bool isEmpty() const;

public slots:
    void processFrames(const QVector<FrameData>& frames);
    void clear();
    void pause(bool paused = true);
    void resume() { pause(false); }

signals:
    void scrollPositionChanged(double position);
    void dataRangeChanged(int columns);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private slots:
    void onUpdateTimeout();

private:
    // Gestión de datos
    void appendColumn(const QVector<float>& magnitudes);
    void updateVisibleRange();
    void updateImageBuffer();

    // Renderizado
    QRgb colorForDb(float db) const;
    void buildColorMap();
    void buildRoesusColorMap();
    void buildViridisColorMap();
    void buildPlasmaColorMap();
    void buildGrayscaleColorMap();

    // Optimizaciones
    void optimizeMemoryUsage();
    bool shouldUpdateImage() const;

    // Interacción
    void handleMouseInteraction(const QPoint& pos);

    // Miembros de datos
    mutable QMutex               m_mutex;
    SpectrogramConfig            m_cfg;
    QVector<QVector<float>>      m_columns;
    QImage                       m_image;
    std::unique_ptr<QTimer>      m_timer;

    // Estado de renderizado
    bool                         m_needsUpdate;
    bool                         m_paused;
    int                          m_visibleStart;
    int                          m_visibleEnd;

    // Paleta de colores
    QVector<QRgb>                m_colorMap;
    ColorMapType                 m_colorMapType;

    // Interacción del usuario
    bool                         m_dragging;
    QPoint                       m_lastMousePos;
    double                       m_manualScrollPos;

    // Optimizaciones
    QSize                        m_lastSize;
    int                          m_lastColumnCount;

    // Cache para evitar recalculos
    mutable float                m_dbRange;
    mutable float                m_dbScale;
};

#endif // SPECTROGRAM_RENDERER_H
