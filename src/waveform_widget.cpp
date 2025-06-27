#include "include/waveform_widget.h"
#include <QPainter>
#include <QtMath>

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(400, 100);
    initializeBuffer();

    // 60 FPS
    m_updateTimer.setInterval(1000/60);
    connect(&m_updateTimer, &QTimer::timeout, this, &WaveformWidget::updateDisplay);
    m_updateTimer.start();
}

void WaveformWidget::setSampleRate(int sampleRate) {
    QMutexLocker locker(&m_mutex);
    m_sampleRate = sampleRate;
    initializeBuffer();
}

void WaveformWidget::setTimeWindow(int seconds) {
    QMutexLocker locker(&m_mutex);
    m_timeWindow = seconds;
    initializeBuffer();
}

void WaveformWidget::setRefreshRate(int fps) {
    m_updateTimer.setInterval(qMax(1, 1000/fps));
}

void WaveformWidget::setWaveColor(const QColor& c) {
    m_waveColor = c;
    update();
}

void WaveformWidget::setBackgroundColor(const QColor& c) {
    m_bgColor = c;
    update();
}

void WaveformWidget::initializeBuffer() {
    int size = m_sampleRate * m_timeWindow;
    m_buffer.fill(0.0f, size);
    m_writePos = 0;
}

void WaveformWidget::addSamples(const QVector<float>& samples) {
    QMutexLocker locker(&m_mutex);
    if (m_buffer.isEmpty()) return;
    for (float s : samples) {
        m_buffer[m_writePos] = qBound(-1.0f, s, 1.0f);
        m_writePos = (m_writePos + 1) % m_buffer.size();
    }
}

void WaveformWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), m_bgColor);

    QMutexLocker locker(&m_mutex);
    if (m_buffer.isEmpty()) return;

    int W = width();
    int H = height();
    int centerY = H/2;
    int groupSize = qMax(1, m_buffer.size()/W);

    QVector<QPointF> pts;
    pts.reserve(W);
    for (int x=0; x<W; ++x) {
        int start = (m_writePos + x*groupSize) % m_buffer.size();
        float peak = 0;
        for (int k=0; k<groupSize; ++k) {
            int idx = (start + k) % m_buffer.size();
            peak = qMax(peak, qAbs(m_buffer[idx]));
        }
        float y = centerY - (peak * centerY * 0.8f);
        pts.append({float(x), y});
    }

    p.setPen(QPen(m_waveColor,1));
    p.drawPolyline(pts.constData(), pts.size());
}

void WaveformWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
}

void WaveformWidget::updateDisplay() {
    update();
}
