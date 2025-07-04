#include "spectrogram_renderer.h"
#include <QPainter>
#include <QPalette>
#include <QtMath>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QApplication>

SpectrogramRenderer::SpectrogramRenderer(QWidget* parent)
    : QWidget(parent)
    , m_timer(std::make_unique<QTimer>(this))
    , m_needsUpdate(false)
    , m_paused(false)
    , m_visibleStart(0)
    , m_visibleEnd(0)
    , m_colorMapType(ColorMapType::Roesus)
    , m_dragging(false)
    , m_manualScrollPos(0.0)
    , m_lastColumnCount(0)
    , m_dbRange(0.0f)
    , m_dbScale(1.0f)
{
    setAutoFillBackground(true);
    setPalette(QPalette(Qt::black));
    setMouseTracking(true);

    m_timer->setInterval(m_cfg.updateInterval);
    connect(m_timer.get(), &QTimer::timeout, this, &SpectrogramRenderer::onUpdateTimeout);
    m_timer->start();

    buildColorMap();

    // Precalcular valores constantes
    m_dbRange = m_cfg.maxDb - m_cfg.minDb;
    m_dbScale = 255.0f / m_dbRange;
}

SpectrogramRenderer::~SpectrogramRenderer() {
    if (m_timer && m_timer->isActive()) {
        m_timer->stop();
    }
}

void SpectrogramRenderer::setConfig(const SpectrogramConfig& cfg) {
    if (!cfg.isValid()) {
        qWarning("SpectrogramRenderer: Invalid configuration provided");
        return;
    }

    QMutexLocker lock(&m_mutex);

    bool needsImageUpdate = (m_cfg.fftSize != cfg.fftSize ||
                             m_cfg.blockWidth != cfg.blockWidth ||
                             m_cfg.minDb != cfg.minDb ||
                             m_cfg.maxDb != cfg.maxDb);

    bool needsTimerUpdate = (m_cfg.updateInterval != cfg.updateInterval);

    m_cfg = cfg;

    if (needsTimerUpdate) {
        m_timer->setInterval(m_cfg.updateInterval);
    }

    if (needsImageUpdate) {
        m_columns.clear();
        m_image = QImage();
        // Recalcular valores de escala
        m_dbRange = m_cfg.maxDb - m_cfg.minDb;
        m_dbScale = 255.0f / m_dbRange;
    }

    m_needsUpdate = true;
}

SpectrogramConfig SpectrogramRenderer::config() const {
    QMutexLocker lock(&m_mutex);
    return m_cfg;
}

void SpectrogramRenderer::setColorMap(ColorMapType type) {
    QMutexLocker lock(&m_mutex);
    if (m_colorMapType != type) {
        m_colorMapType = type;
        buildColorMap();
        m_needsUpdate = true;
    }
}

void SpectrogramRenderer::setScrollPosition(double position) {
    position = qBound(0.0, position, 1.0);
    QMutexLocker lock(&m_mutex);
    m_manualScrollPos = position;
    if (!m_cfg.autoScroll) {
        m_needsUpdate = true;
    }
}

double SpectrogramRenderer::scrollPosition() const {
    QMutexLocker lock(&m_mutex);
    return m_manualScrollPos;
}

int SpectrogramRenderer::columnCount() const {
    QMutexLocker lock(&m_mutex);
    return m_columns.size();
}

bool SpectrogramRenderer::isEmpty() const {
    QMutexLocker lock(&m_mutex);
    return m_columns.isEmpty();
}

void SpectrogramRenderer::processFrames(const QVector<FrameData>& frames) {
    if (frames.isEmpty() || m_paused) return;

    QMutexLocker lock(&m_mutex);
    bool dataAdded = false;

    for (const auto& frame : frames) {
        if (!frame.spectrum.isEmpty()) {
            appendColumn(frame.spectrum);
            dataAdded = true;
        }
    }

    if (dataAdded) {
        m_needsUpdate = true;
        emit dataRangeChanged(m_columns.size());
    }
}

void SpectrogramRenderer::clear() {
    QMutexLocker lock(&m_mutex);
    m_columns.clear();
    m_image = QImage();
    m_needsUpdate = true;
    m_lastColumnCount = 0;
    emit dataRangeChanged(0);
}

void SpectrogramRenderer::pause(bool paused) {
    QMutexLocker lock(&m_mutex);
    m_paused = paused;
    if (m_paused) {
        m_timer->stop();
    } else {
        m_timer->start();
    }
}

void SpectrogramRenderer::appendColumn(const QVector<float>& mags) {
    m_columns.append(mags);

    if (m_cfg.maxColumns > 0 && m_columns.size() > m_cfg.maxColumns) {
        // Usar deque semántica para mejor performance
        int toRemove = m_columns.size() - m_cfg.maxColumns;
        for (int i = 0; i < toRemove; ++i) {
            m_columns.removeFirst();
        }
    }

    optimizeMemoryUsage();
}

void SpectrogramRenderer::onUpdateTimeout() {
    if (!shouldUpdateImage()) return;

    updateVisibleRange();
    updateImageBuffer();
    update();
    m_needsUpdate = false;
}

bool SpectrogramRenderer::shouldUpdateImage() const {
    return m_needsUpdate && !m_paused && !m_columns.isEmpty();
}

void SpectrogramRenderer::updateVisibleRange() {
    int total = m_columns.size();
    int maxVis = (m_cfg.maxColumns > 0) ? qMin(total, m_cfg.maxColumns) : total;

    if (m_cfg.autoScroll) {
        m_visibleEnd = total;
        m_visibleStart = qMax(0, m_visibleEnd - maxVis);
    } else {
        // Scroll manual basado en posición
        int scrollRange = qMax(0, total - maxVis);
        m_visibleStart = qRound(m_manualScrollPos * scrollRange);
        m_visibleEnd = qMin(total, m_visibleStart + maxVis);
    }

    // Emitir cambio de posición si es necesario
    if (total > 0) {
        double currentPos = static_cast<double>(m_visibleStart) / qMax(1, total - maxVis);
        emit scrollPositionChanged(currentPos);
    }
}

void SpectrogramRenderer::updateImageBuffer() {
    int cols = m_visibleEnd - m_visibleStart;
    int rows = m_cfg.fftSize / 2 + 1;

    if (cols <= 0 || rows <= 0) return;

    // Verificar si necesitamos crear una nueva imagen
    QSize newSize(cols * m_cfg.blockWidth, rows);
    if (m_image.size() != newSize) {
        m_image = QImage(newSize, QImage::Format_RGB32);
    }

    m_image.fill(Qt::black);

    // Renderizado optimizado
    for (int i = m_visibleStart; i < m_visibleEnd; ++i) {
        int col = i - m_visibleStart;
        const auto& mags = m_columns[i];
        int actualRows = qMin(rows, mags.size());

        for (int j = 0; j < actualRows; ++j) {
            QRgb color = colorForDb(mags[j]);
            int yPos = rows - 1 - j;

            // Optimización: escribir directamente en scanLine
            if (m_cfg.blockWidth == 1) {
                m_image.setPixel(col, yPos, color);
            } else {
                QRgb* scanLine = reinterpret_cast<QRgb*>(m_image.scanLine(yPos));
                for (int x = 0; x < m_cfg.blockWidth; ++x) {
                    scanLine[col * m_cfg.blockWidth + x] = color;
                }
            }
        }
    }
}

QRgb SpectrogramRenderer::colorForDb(float db) const {
    // Usar valores precalculados para mejor performance
    float norm = (db - m_cfg.minDb) * m_dbScale / 255.0f;
    norm = qBound(0.0f, norm, 1.0f);
    int idx = static_cast<int>(norm * 255);
    return m_colorMap[idx];
}

void SpectrogramRenderer::buildColorMap() {
    m_colorMap.resize(256);

    switch (m_colorMapType) {
    case ColorMapType::Roesus:
        buildRoesusColorMap();
        break;
    case ColorMapType::Viridis:
        buildViridisColorMap();
        break;
    case ColorMapType::Plasma:
        buildPlasmaColorMap();
        break;
    case ColorMapType::Grayscale:
        buildGrayscaleColorMap();
        break;
    }
}

void SpectrogramRenderer::buildRoesusColorMap() {
    // Paleta original mejorada
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

void SpectrogramRenderer::buildViridisColorMap() {
    // Aproximación a la paleta Viridis
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        int r = qRound(68 + t * (253 - 68));
        int g = qRound(1 + t * (231 - 1));
        int b = qRound(84 + t * (37 - 84));
        m_colorMap[i] = qRgb(r, g, b);
    }
}

void SpectrogramRenderer::buildPlasmaColorMap() {
    // Aproximación a la paleta Plasma
    for (int i = 0; i < 256; ++i) {
        float t = i / 255.0f;
        int r = qRound(13 + t * (240 - 13));
        int g = qRound(8 + t * (249 - 8));
        int b = qRound(135 + t * (33 - 135));
        m_colorMap[i] = qRgb(r, g, b);
    }
}

void SpectrogramRenderer::buildGrayscaleColorMap() {
    for (int i = 0; i < 256; ++i) {
        m_colorMap[i] = qRgb(i, i, i);
    }
}

void SpectrogramRenderer::optimizeMemoryUsage() {
    // Optimizar uso de memoria cada cierto número de columnas
    if (m_columns.size() % 100 == 0) {
        m_columns.squeeze();
    }
}

void SpectrogramRenderer::wheelEvent(QWheelEvent* event) {
    if (!m_cfg.autoScroll) {
        QMutexLocker lock(&m_mutex);
        double delta = event->angleDelta().y() / 1200.0; // Sensibilidad
        m_manualScrollPos = qBound(0.0, m_manualScrollPos - delta, 1.0);
        m_needsUpdate = true;
        event->accept();
    }
}

void SpectrogramRenderer::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMousePos = event->pos();
        event->accept();
    }
}

void SpectrogramRenderer::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging && !m_cfg.autoScroll) {
        QMutexLocker lock(&m_mutex);
        int deltaX = event->pos().x() - m_lastMousePos.x();
        double scrollDelta = static_cast<double>(deltaX) / width();
        m_manualScrollPos = qBound(0.0, m_manualScrollPos - scrollDelta, 1.0);
        m_needsUpdate = true;
        m_lastMousePos = event->pos();
        event->accept();
    }
}

void SpectrogramRenderer::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false); // Optimización
    painter.fillRect(rect(), Qt::black);

    QMutexLocker lock(&m_mutex);
    if (m_image.isNull()) return;

    // Renderizado optimizado
    QRect sourceRect(0, 0, m_image.width(), m_image.height());
    painter.drawImage(rect(), m_image, sourceRect);
}

void SpectrogramRenderer::resizeEvent(QResizeEvent* event) {
    QMutexLocker lock(&m_mutex);
    if (m_lastSize != event->size()) {
        m_lastSize = event->size();
        m_needsUpdate = true;
    }
}
