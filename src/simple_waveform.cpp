#include "include/simple_waveform.h"
#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>   // <<-- necesario para QPainterPath
#include <QtMath>

SimpleWaveformWidget::SimpleWaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SimpleWaveformWidget::updateWaveform(const QVector<float>& samples)
{
    if (samples.isEmpty()) return;
    QMutexLocker locker(&m_mutex);
    m_waveform = samples;
    // Si hay más muestras que píxels de ancho, bajamos resolución
    if (m_waveform.size() > width()) {
        QVector<float> down;
        down.reserve(width());
        float ratio = float(m_waveform.size()) / float(width());
        for (int i = 0; i < width(); ++i) {
            down.append(m_waveform[int(i * ratio)]);
        }
        m_waveform = std::move(down);
    }
    update();
}

void SimpleWaveformWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(30,30,30));
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0,200,0), 1));

    QVector<float> data;
    {
        QMutexLocker locker(&m_mutex);
        data = m_waveform;
    }
    if (data.isEmpty()) return;

    int h = height();
    int center = h / 2;

    QPainterPath path;
    path.moveTo(0, center - data[0] * center);
    for (int x = 1; x < data.size(); ++x) {
        float y = center - data[x] * center;
        path.lineTo(x, y);
    }
    painter.drawPath(path);
}
