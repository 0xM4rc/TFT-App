#ifndef SPECTROGRAM_WIDGET_H
#define SPECTROGRAM_WIDGET_H

#include <QWidget>
#include <QImage>
#include <QVector>
#include <QColor>
#include <QGradient>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMenu>
#include <QAction>

class SpectrogramWidget : public QWidget
{
    Q_OBJECT

public:
    enum ColorMap {
        Jet,
        Hot,
        Cool,
        Grayscale,
        Viridis,
        Plasma,
        Inferno
    };

    enum DisplayMode {
        Waterfall,      // Tradicional cascada vertical
        ScrollingLeft,  // Se desplaza hacia la izquierda
        ScrollingRight  // Se desplaza hacia la derecha
    };

    explicit SpectrogramWidget(QWidget *parent = nullptr);
    ~SpectrogramWidget();

    // Configuración básica
    void setColorMap(ColorMap colorMap);
    ColorMap getColorMap() const { return m_colorMap; }

    void setDisplayMode(DisplayMode mode);
    DisplayMode getDisplayMode() const { return m_displayMode; }

    void setMaxHistory(int frames);
    int getMaxHistory() const { return m_maxHistory; }

    void setFrequencyRange(float minFreq, float maxFreq);
    void getFrequencyRange(float& minFreq, float& maxFreq) const;

    void setSampleRate(int sampleRate);
    int getSampleRate() const { return m_sampleRate; }

    // Configuración de visualización
    void setAmplitudeRange(float minDb, float maxDb);
    void getAmplitudeRange(float& minDb, float& maxDb) const;

    void setInterpolation(bool enabled);
    bool getInterpolation() const { return m_interpolationEnabled; }

    void setShowGrid(bool show);
    bool getShowGrid() const { return m_showGrid; }

    void setShowFrequencyLabels(bool show);
    bool getShowFrequencyLabels() const { return m_showFrequencyLabels; }

    void setShowTimeLabels(bool show);
    bool getShowTimeLabels() const { return m_showTimeLabels; }

    void setShowColorBar(bool show);
    bool getShowColorBar() const { return m_showColorBar; }

    // Control de actualización
    void setAutoUpdate(bool enabled);
    bool getAutoUpdate() const { return m_autoUpdate; }

    void setUpdateRate(int fps);
    int getUpdateRate() const { return m_updateRate; }

public slots:
    void updateSpectrogram(const QVector<float>& spectrum);
    void updateSpectrogram(const QVector<QVector<float>>& spectrogramData);
    void clearHistory();
    void saveImage(const QString& filename);
    void copyToClipboard();

signals:
    void frequencyClicked(float frequency);
    void timeClicked(double timeSeconds);
    void amplitudeClicked(float amplitudeDb);
    void selectionChanged(float minFreq, float maxFreq, double startTime, double endTime);

protected:
    // Eventos de Qt
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

    // Funciones de renderizado
    void renderSpectrogram(QPainter& painter);
    void renderGrid(QPainter& painter);
    void renderFrequencyLabels(QPainter& painter);
    void renderTimeLabels(QPainter& painter);
    void renderColorBar(QPainter& painter);
    void renderSelection(QPainter& painter);

    // Utilidades de renderizado
    void updateSpectrogramImage();
    void generateColorMap();
    QColor amplitudeToColor(float amplitudeDb) const;
    float frequencyToY(float frequency) const;
    float yToFrequency(float y) const;
    double timeToX(double timeSeconds) const;
    double xToTime(double x) const;

    // Gestión de datos
    void addSpectrumFrame(const QVector<float>& spectrum);
    void trimHistory();

private slots:
    void updateDisplay();
    void showContextMenu(const QPoint& pos);

private:
    // Configuración de visualización
    ColorMap m_colorMap;
    DisplayMode m_displayMode;
    int m_maxHistory;
    float m_minFrequency;
    float m_maxFrequency;
    int m_sampleRate;
    float m_minAmplitude;
    float m_maxAmplitude;

    // Opciones de visualización
    bool m_interpolationEnabled;
    bool m_showGrid;
    bool m_showFrequencyLabels;
    bool m_showTimeLabels;
    bool m_showColorBar;
    bool m_autoUpdate;
    int m_updateRate;

    // Datos del espectrograma
    QVector<QVector<float>> m_spectrogramData;
    QMutex m_dataMutex;

    // Imagen y renderizado
    QImage m_spectrogramImage;
    bool m_imageNeedsUpdate;
    QVector<QColor> m_colorMapData;

    // Control de tiempo
    QTimer* m_updateTimer;
    QElapsedTimer m_elapsedTimer;
    double m_startTime;

    // Interacción del usuario
    bool m_isSelecting;
    QPoint m_selectionStart;
    QPoint m_selectionEnd;
    QRect m_selectionRect;

    // Menú contextual
    QMenu* m_contextMenu;
    QAction* m_actionSaveImage;
    QAction* m_actionCopyClipboard;
    QAction* m_actionClearHistory;
    QAction* m_actionColorMapJet;
    QAction* m_actionColorMapHot;
    QAction* m_actionColorMapCool;
    QAction* m_actionColorMapGrayscale;
    QAction* m_actionColorMapViridis;
    QAction* m_actionToggleGrid;
    QAction* m_actionToggleLabels;
    QAction* m_actionToggleColorBar;

    // Márgenes y espaciado
    static const int LEFT_MARGIN = 60;
    static const int RIGHT_MARGIN = 80;  // Para colorbar
    static const int TOP_MARGIN = 20;
    static const int BOTTOM_MARGIN = 40;
    static const int COLORBAR_WIDTH = 20;
    static const int COLORBAR_MARGIN = 10;

    // Configuración por defecto
    static const int DEFAULT_MAX_HISTORY = 200;
    static const int DEFAULT_UPDATE_RATE = 30; // FPS
    static const float DEFAULT_MIN_FREQUENCY;
    static const float DEFAULT_MAX_FREQUENCY;
    static const float DEFAULT_MIN_AMPLITUDE;
    static const float DEFAULT_MAX_AMPLITUDE;

    void setupUI();
    void createContextMenu();
    void updateGeometry();
};

#endif // SPECTROGRAM_WIDGET_H
