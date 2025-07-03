#ifndef WAVEFORM_WIDGET_H
#define WAVEFORM_WIDGET_H

#include "dsp_worker.h"
#include <QWidget>
#include <QVector>
#include <QMutex>
#include <QColor>

class QPainter;
class QPainterPath;

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    // Configuración de ventana temporal
    void setSampleRate(int sampleRate) {
        if (sampleRate > 0) {
            m_sampleRate = sampleRate;
            recalcBuffer();
        }
    }

    void setTimeWindowSeconds(float seconds) {
        if (seconds > 0.0f) {
            m_timeWindowSeconds = seconds;
            recalcBuffer();
        }
    }

    // Agregar datos de waveform
    void appendFrames(const QVector<FrameData>& frames);

    // Configuración de estilo
    void setBackgroundColor(const QColor& color) { m_bgColor = color; update(); }
    void setGridColor(const QColor& color) { m_gridColor = color; update(); }
    void setWaveColor(const QColor& color) { m_waveColor = color; update(); }
    void setShowGrid(bool show) { m_showGrid = show; update(); }

    // Configuración del buffer
    void setWaveformSize(int size);

    // Método para limpiar completamente el waveform
    void clearWaveform();

    // Getters
    int sampleRate() const { return m_sampleRate; }
    float timeWindowSeconds() const { return m_timeWindowSeconds; }
    int totalSamples() const { return m_totalSamples; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void recalcBuffer();

    // Métodos de renderizado
    void drawGrid(QPainter& painter, int width, int height, int midY);
    void drawWaveform(QPainter& painter, const QVector<float>& buffer,
                      int writeIndex, int totalSamples,
                      int width, int height, int midY);

    // Métodos de renderizado mejorados con control de path
    void drawWaveformDownsampled(QPainterPath& path, const QVector<float>& buffer,
                                 int writeIndex, int totalSamples,
                                 int width, int midY, int maxAmplitude,
                                 float samplesPerPixel, bool& pathStarted);

    void drawWaveformUpsampled(QPainterPath& path, const QVector<float>& buffer,
                               int writeIndex, int totalSamples,
                               int width, int midY, int maxAmplitude,
                               float samplesPerPixel, bool& pathStarted);

    // Datos del buffer circular
    QVector<float> m_buffer;
    int m_writeIndex = 0;
    int m_totalSamples = 0;

    // Configuración temporal
    float m_timeWindowSeconds = 5.0f;
    int m_sampleRate = 44100;

    // Configuración visual
    QColor m_bgColor = QColor(30, 30, 30);
    QColor m_gridColor = QColor(60, 60, 80);
    QColor m_waveColor = QColor(0, 200, 255);
    bool m_showGrid = true;

    // Thread safety
    mutable QMutex m_bufferMutex;
};

#endif // WAVEFORM_WIDGET_H
