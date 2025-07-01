// waveform_widget.h
#ifndef WAVEFORM_WIDGET_H
#define WAVEFORM_WIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QVector>
#include <QPair>
#include <QColor>
#include <QBrush>
#include <QPen>

class AudioDb;

struct WaveformConfig {
    QColor backgroundColor = QColor(20, 20, 30);           // Fondo oscuro
    QColor waveformColor = QColor(0, 150, 255);           // Azul para la onda
    QColor centerLineColor = QColor(80, 80, 80);          // Línea central
    QColor gridColor = QColor(40, 40, 50);                // Rejilla

    int pixelsPerBlock = 2;        // Pixels por bloque de audio
    int maxVisibleBlocks = 1000;   // Máximo de bloques visibles
    bool showGrid = true;          // Mostrar rejilla
    bool showCenterLine = true;    // Mostrar línea central
    bool antiAliasing = true;      // Suavizado

    float amplitudeScale = 1.0f;   // Escala de amplitud (zoom vertical)
    int updateIntervalMs = 50;     // Intervalo de actualización en ms
};

class WaveformWidget : public QWidget
{
    Q_OBJECT

public:
    explicit WaveformWidget(AudioDb* db, QWidget* parent = nullptr);
    ~WaveformWidget();

    // Configuración
    void setConfig(const WaveformConfig& config);
    WaveformConfig getConfig() const { return m_config; }

    // Control de visualización
    void setAmplitudeScale(float scale);
    void setPixelsPerBlock(int pixels);
    void setMaxVisibleBlocks(int blocks);

    // Control de datos
    void clearWaveform();
    void refreshFromDatabase();
    void setAutoUpdate(bool enabled);

    // Información
    int getTotalBlocks() const { return m_peakData.size(); }
    QString getStatusInfo() const;

public slots:
    void onNewPeakData(float minValue, float maxValue, qint64 timestamp);
    void updateDisplay();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    void setupTimer();
    void loadPeaksFromDatabase();
    void drawWaveform(QPainter& painter);
    void drawGrid(QPainter& painter);
    void drawCenterLine(QPainter& painter);
    void calculateDisplayRange();

    // Conversión de coordenadas
    float sampleToY(float sample) const;
    int blockToX(int blockIndex) const;
    void onPollDatabase();

private:
    AudioDb* m_db;
    WaveformConfig m_config;

    // Datos de picos (min, max)
    QVector<QPair<float, float>> m_peakData;

    // Control de visualización
    int m_displayStartBlock = 0;    // Primer bloque visible
    int m_displayEndBlock = 0;      // Último bloque visible
    int m_visibleBlocks = 0;        // Bloques que caben en pantalla

    // Timer para actualizaciones
    QTimer* m_updateTimer;
    bool m_autoUpdate = true;

    // Cache para rendimiento
    bool m_needsRecalculation = true;
    int m_lastKnownBlocks = 0;

    // Interacción
    bool m_dragging = false;
    QPoint m_lastMousePos;
    float m_horizontalOffset = 0.0f;

    QTimer*   m_pollTimer    = nullptr;
    int       m_lastKnownBlock = 0;
};

#endif // WAVEFORM_WIDGET_H
