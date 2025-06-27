#ifndef WAVEFORM_WIDGET_H
#define WAVEFORM_WIDGET_H

#include <QWidget>
#include <QVector>
#include <QTimer>
#include <QMutex>

class WaveformWidget : public QWidget {
    Q_OBJECT

public:
    explicit WaveformWidget(QWidget* parent = nullptr);

    void setSampleRate(int sampleRate);
    void setTimeWindow(int seconds);
    void setRefreshRate(int fps = 60);
    void setWaveColor(const QColor& c);
    void setBackgroundColor(const QColor& c);

public slots:
    void addSamples(const QVector<float>& samples);

protected:
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void initializeBuffer();
    void updateDisplay();

    QMutex           m_mutex;
    QVector<float>   m_buffer;
    int              m_writePos{0};
    int              m_sampleRate{48000};
    int              m_timeWindow{30};
    QTimer           m_updateTimer;
    QColor           m_waveColor{70,130,180};
    QColor           m_bgColor{0,0,0};
};

#endif // WAVEFORM_WIDGET_H
