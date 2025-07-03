#ifndef SPECTROGRAM_RENDERER_H
#define SPECTROGRAM_RENDERER_H

#include <QWidget>
#include <QVector>
#include <QMutex>
#include <QTimer>
#include <QImage>
#include "dsp_worker.h"

struct SpectrogramConfig {
    int    fftSize        = 1024;      // debe coincidir con DSPConfig.fftSize
    int    sampleRate     = 44100;
    int    blockWidth     = 2;         // ancho en píxeles por columna
    int    updateInterval = 30;        // ms entre repintados
    int    maxColumns     = 500;       // número máximo de columnas (tiempo)
    bool   autoScroll     = true;      // desplazamiento automático
    float  minDb          = -100.0f;   // piso en dB (negro)
    float  maxDb          =   0.0f;    // tope en dB (blanco)
};

class SpectrogramRenderer : public QWidget {
    Q_OBJECT
public:
    explicit SpectrogramRenderer(QWidget* parent = nullptr);
    ~SpectrogramRenderer() override;

    void setConfig(const SpectrogramConfig& cfg);
    SpectrogramConfig config() const;

public slots:
    void processFrames(const QVector<FrameData>& frames);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onUpdateTimeout();

private:
    void appendColumn(const QVector<float>& magnitudes);
    void updateImageBuffer();
    void updateVisibleRange();
    QRgb  colorForDb(float db) const;
    void  buildColorMap();

    mutable QMutex               m_mutex;
    SpectrogramConfig            m_cfg;
    QVector< QVector<float> >    m_columns;
    QImage                       m_image;
    QTimer*                      m_timer;
    bool                         m_needsUpdate;
    int                          m_visibleStart;
    int                          m_visibleEnd;
    QVector<QRgb>                m_colorMap;   // paleta “roesus”
};

#endif // SPECTROGRAM_RENDERER_H
