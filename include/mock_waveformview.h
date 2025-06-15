#ifndef MOCK_WAVEFORMVIEW_H
#define MOCK_WAVEFORMVIEW_H

#include <QWidget>
#include <QVector>
#include <QMutex>
#include "include/audiochunk.h"
#include <QPainter>

class MockWaveformView : public QWidget {
    Q_OBJECT
public:
    explicit MockWaveformView(QWidget *parent = nullptr)
        : QWidget(parent) {}

public slots:
    void addChunk(const AudioChunk &chunk) {
        QMutexLocker locker(&m_mutex);
        // Convierte bytes Int16 a muestras [-1,1]
        const int16_t *samples = reinterpret_cast<const int16_t*>(chunk.data.constData());
        int n = chunk.data.size() / sizeof(int16_t);
        for (int i = 0; i < n; ++i)
            m_samples.append(samples[i] / 32768.0f);

        // Limita para no crecer sin fin
        if (m_samples.size() > width())
            m_samples.remove(0, m_samples.size() - width());

        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.setPen(Qt::green);

        QMutexLocker locker(&m_mutex);
        int h = height();
        int w = m_samples.size();
        for (int x = 1; x < w; ++x) {
            float y0 = (1 - m_samples[x-1]) * h/2;
            float y1 = (1 - m_samples[x  ]) * h/2;
            p.drawLine(x-1, y0, x, y1);
        }
    }

private:
    QVector<float> m_samples;
    QMutex         m_mutex;
};

#endif // MOCK_WAVEFORMVIEW_H
