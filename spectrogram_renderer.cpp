#include "spectrogram_renderer.h"
#include <QPainter>
#include <QPalette>
#include <QtMath>

SpectrogramRenderer::SpectrogramRenderer(QWidget* parent)
    : QWidget(parent)
    , m_timer(new QTimer(this))
    , m_needsUpdate(false)
    , m_visibleStart(0)
    , m_visibleEnd(0)
{
    setAutoFillBackground(true);
    setPalette(QPalette(Qt::black));
    m_timer->setInterval(m_cfg.updateInterval);
    connect(m_timer, &QTimer::timeout, this, &SpectrogramRenderer::onUpdateTimeout);
    m_timer->start();

    buildColorMap();  // construir paleta “roesus”
}

SpectrogramRenderer::~SpectrogramRenderer() {
    if (m_timer->isActive()) m_timer->stop();
}

void SpectrogramRenderer::setConfig(const SpectrogramConfig& cfg) {
    QMutexLocker lock(&m_mutex);
    m_cfg = cfg;
    m_timer->setInterval(m_cfg.updateInterval);
    m_columns.clear();
    m_needsUpdate = true;
}

SpectrogramConfig SpectrogramRenderer::config() const {
    QMutexLocker lock(&m_mutex);
    return m_cfg;
}

void SpectrogramRenderer::processFrames(const QVector<FrameData>& frames) {
    if (frames.isEmpty()) return;
    QMutexLocker lock(&m_mutex);
    for (const auto& frame : frames) {
        if (!frame.spectrum.isEmpty()) {
            appendColumn(frame.spectrum);
        }
    }
    m_needsUpdate = true;
}

void SpectrogramRenderer::clear() {
    QMutexLocker lock(&m_mutex);
    m_columns.clear();
    m_image = QImage();
    m_needsUpdate = true;
}

void SpectrogramRenderer::appendColumn(const QVector<float>& mags) {
    m_columns.append(mags);
    if (m_cfg.maxColumns > 0 && m_columns.size() > m_cfg.maxColumns) {
        m_columns.removeFirst();
    }
}

void SpectrogramRenderer::onUpdateTimeout() {
    if (!m_needsUpdate) return;
    updateVisibleRange();
    updateImageBuffer();
    update();
    m_needsUpdate = false;
}

void SpectrogramRenderer::updateVisibleRange() {
    int total = m_columns.size();
    int maxVis = (m_cfg.maxColumns > 0) ? qMin(total, m_cfg.maxColumns) : total;
    if (m_cfg.autoScroll) {
        m_visibleEnd   = total;
        m_visibleStart = qMax(0, m_visibleEnd - maxVis);
    } else {
        m_visibleStart = 0;
        m_visibleEnd   = maxVis;
    }
}

void SpectrogramRenderer::updateImageBuffer() {
    int cols = m_visibleEnd - m_visibleStart;
    int rows = m_cfg.fftSize/2 + 1;
    if (cols <= 0 || rows <= 0) return;

    m_image = QImage(cols * m_cfg.blockWidth, rows, QImage::Format_RGB32);
    m_image.fill(Qt::black);

    for (int i = m_visibleStart; i < m_visibleEnd; ++i) {
        int col = i - m_visibleStart;
        const auto& mags = m_columns[i];
        for (int j = 0; j < rows; ++j) {
            QRgb c = colorForDb(mags[j]);
            for (int x = 0; x < m_cfg.blockWidth; ++x) {
                m_image.setPixel(col * m_cfg.blockWidth + x,
                                 rows - 1 - j,
                                 c);
            }
        }
    }
}

QRgb SpectrogramRenderer::colorForDb(float db) const {
    float norm = (db - m_cfg.minDb) / (m_cfg.maxDb - m_cfg.minDb);
    norm = qBound(0.0f, norm, 1.0f);
    int idx = qRound(norm * 255);
    return m_colorMap[idx];
}

void SpectrogramRenderer::buildColorMap() {
    m_colorMap.resize(256);
    QColor c0(128, 0, 64);    // rosa oscuro
    QColor c1(255, 0, 128);   // magenta
    QColor c2(255, 255, 0);   // amarillo

    for (int i = 0; i < 256; ++i) {
        if (i < 128) {
            float t = i / 127.0f;
            int r = qRound(c0.red()   + t * (c1.red()   - c0.red()));
            int g = qRound(c0.green() + t * (c1.green() - c0.green()));
            int b = qRound(c0.blue()  + t * (c1.blue()  - c0.blue()));
            m_colorMap[i] = qRgb(r, g, b);
        } else {
            float t = (i - 128) / 127.0f;
            int r = qRound(c1.red()   + t * (c2.red()   - c1.red()));
            int g = qRound(c1.green() + t * (c2.green() - c1.green()));
            int b = qRound(c1.blue()  + t * (c2.blue()  - c1.blue()));
            m_colorMap[i] = qRgb(r, g, b);
        }
    }
}

void SpectrogramRenderer::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);

    QMutexLocker lock(&m_mutex);
    if (m_image.isNull()) return;

    QRect src(0, 0, m_image.width(), m_image.height());
    painter.drawImage(rect(), m_image, src);
}

void SpectrogramRenderer::resizeEvent(QResizeEvent*) {
    QMutexLocker lock(&m_mutex);
    m_needsUpdate = true;
}
