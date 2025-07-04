#include "waveform_render.h"
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <algorithm>
#include <cmath>

WaveformRenderer::WaveformRenderer(QWidget* parent)
    : QWidget(parent)
    , m_maxAmplitude(1.0f)
    , m_zoom(1.0f)
    , m_scrollOffset(0)
    , m_paused(false)
    , m_needsUpdate(false)
    , m_visibleStartIndex(0)
    , m_visibleEndIndex(0)
    , m_totalBlocks(0)
    , m_latestTimestamp(0)
{
    // Configurar el widget
    setMinimumSize(400, 100);
    setMouseTracking(true);

    setContentsMargins(0, 0, 0, 0);

    // Configurar el timer de actualización
    m_updateTimer = new QTimer(this);
    connect(m_updateTimer, &QTimer::timeout, this, &WaveformRenderer::updateDisplay);
    m_updateTimer->start(m_config.updateInterval);

    // Configurar colores por defecto
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, m_config.backgroundColor);
    setPalette(palette);
    setAutoFillBackground(true);

    qDebug() << "WaveformRenderer inicializado";
}

WaveformRenderer::~WaveformRenderer()
{
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void WaveformRenderer::setConfig(const WaveformConfig& config)
{
    QMutexLocker locker(&m_mutex);
    m_config = config;

    // Actualizar timer
    if (m_updateTimer) {
        m_updateTimer->setInterval(m_config.updateInterval);
    }

    // Actualizar colores
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, m_config.backgroundColor);
    setPalette(palette);

    m_needsUpdate = true;
    update();
}

WaveformConfig WaveformRenderer::getConfig() const
{
    QMutexLocker locker(&m_mutex);
    return m_config;
}

int WaveformRenderer::getBlockCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_blocks.size();
}

int WaveformRenderer::getVisibleBlocks() const
{
    QMutexLocker locker(&m_mutex);
    return m_visibleEndIndex - m_visibleStartIndex;
}

qint64 WaveformRenderer::getLatestTimestamp() const
{
    QMutexLocker locker(&m_mutex);
    return m_latestTimestamp;
}

void WaveformRenderer::processFrames(const QVector<FrameData>& frames)
{
    if (m_paused || frames.isEmpty()) {
        return;
    }

    QMutexLocker locker(&m_mutex);

    for (const auto& frame : frames) {
        if (frame.waveform.isEmpty()) {
            continue;
        }

        WaveformBlock block;
        block.blockIndex = m_totalBlocks;
        block.timestamp = frame.timestamp;
        block.sampleOffset = frame.sampleOffset;
        block.samples = frame.waveform;

        // Calcular estadísticas del bloque
        calculateBlockStats(block);

        // Añadir el bloque
        addBlock(block);

        m_totalBlocks++;
        m_latestTimestamp = frame.timestamp;
    }

    m_needsUpdate = true;
    emit waveformUpdated(m_blocks.size());
}

void WaveformRenderer::clear()
{
    QMutexLocker locker(&m_mutex);
    m_blocks.clear();
    m_maxAmplitude = 1.0f;
    m_scrollOffset = 0;
    m_visibleStartIndex = 0;
    m_visibleEndIndex = 0;
    m_totalBlocks = 0;
    m_latestTimestamp = 0;
    m_needsUpdate = true;
    update();
}

void WaveformRenderer::setPaused(bool paused)
{
    m_paused = paused;
    if (!paused) {
        m_needsUpdate = true;
    }
}

void WaveformRenderer::setZoom(float zoom)
{
    m_zoom = std::max(0.1f, std::min(10.0f, zoom));
    m_needsUpdate = true;
    update();
}

void WaveformRenderer::updateDisplay()
{
    if (!m_needsUpdate) {
        return;
    }

    updateVisibleRange();
    update();
    m_needsUpdate = false;
}

void WaveformRenderer::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Obtener el área de dibujo completa
    QRect drawRect = rect();

    // Limpiar fondo
    painter.fillRect(drawRect, m_config.backgroundColor);

    QMutexLocker locker(&m_mutex);

    if (m_blocks.isEmpty()) {
        // Mostrar texto de estado centrado
        painter.setPen(Qt::white);
        painter.drawText(drawRect, Qt::AlignCenter, "Esperando datos de audio...");
        return;
    }

    // Calcular dimensiones usando el área completa
    int height = drawRect.height();
    int width = drawRect.width();
    int blockTotalWidth = m_config.blockWidth + m_config.blockSpacing;

    // Verificar que tenemos espacio suficiente
    if (width <= 0 || height <= 0 || blockTotalWidth <= 0) {
        return;
    }

    // Calcular cuántos bloques caben en la pantalla
    int maxVisibleBlocks = width / blockTotalWidth;

    // Determinar rango de bloques a dibujar
    int startIndex = m_config.scrolling ?
                         std::max(0, static_cast<int>(m_blocks.size()) - maxVisibleBlocks) :
                         m_visibleStartIndex;

    int endIndex = std::min(static_cast<int>(m_blocks.size()),
                            startIndex + maxVisibleBlocks);

    // Dibujar línea central
    painter.setPen(QPen(Qt::gray, 1, Qt::DashLine));
    painter.drawLine(drawRect.left(), height / 2, drawRect.right(), height / 2);

    // Dibujar bloques comenzando desde el margen izquierdo
    int x = drawRect.left();  // Comenzar desde el borde izquierdo real
    for (int i = startIndex; i < endIndex; ++i) {
        if (i < m_blocks.size()) {
            drawBlock(painter, m_blocks[i], x, height);
        }
        x += blockTotalWidth;

        // Verificar si nos salimos del área visible
        if (x > drawRect.right()) {
            break;
        }
    }

    // Dibujar información de estado en la parte superior
    painter.setPen(Qt::white);
    QRect textRect = QRect(drawRect.left() + 5, drawRect.top() + 5,
                           drawRect.width() - 10, 20);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop,
                     QString("Bloques: %1 | Zoom: %2x | Max: %3")
                         .arg(m_blocks.size())
                         .arg(m_zoom, 0, 'f', 1)
                         .arg(m_maxAmplitude, 0, 'f', 3));
}

void WaveformRenderer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateVisibleRange();
    m_needsUpdate = true;
}

void WaveformRenderer::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        int blockIndex = getBlockAtPosition(event->x());
        if (blockIndex >= 0 && blockIndex < m_blocks.size()) {
            emit blockClicked(m_blocks[blockIndex].blockIndex,
                              m_blocks[blockIndex].timestamp);
        }
    }
}

void WaveformRenderer::wheelEvent(QWheelEvent* event)
{
    // Zoom con la rueda del ratón
    float zoomFactor = 1.2f;
    if (event->angleDelta().y() > 0) {
        setZoom(m_zoom * zoomFactor);
    } else {
        setZoom(m_zoom / zoomFactor);
    }

    event->accept();
}

void WaveformRenderer::addBlock(const WaveformBlock& block)
{
    m_blocks.append(block);

    // Mantener solo los bloques visibles si hay límite
    if (m_config.maxVisibleBlocks > 0 &&
        m_blocks.size() > m_config.maxVisibleBlocks) {
        m_blocks.removeFirst();
    }

    // Actualizar amplitud máxima para auto-escala
    if (m_config.autoScale) {
        m_maxAmplitude = std::max(m_maxAmplitude,
                                  std::max(std::abs(block.minValue),
                                           std::abs(block.maxValue)));
    }
}

void WaveformRenderer::calculateBlockStats(WaveformBlock& block)
{
    if (block.samples.isEmpty()) {
        return;
    }

    float min = block.samples[0];
    float max = block.samples[0];
    float sum = 0.0f;

    for (float sample : block.samples) {
        min = std::min(min, sample);
        max = std::max(max, sample);
        sum += sample * sample;
    }

    block.minValue = min;
    block.maxValue = max;
    block.rmsValue = std::sqrt(sum / block.samples.size());
}

void WaveformRenderer::drawBlock(QPainter& painter, const WaveformBlock& block, int x, int height)
{
    if (m_config.showPeaks) {
        drawPeaks(painter, block, x, height);
    }

    if (m_config.showRMS) {
        drawRMS(painter, block, x, height);
    }
}

void WaveformRenderer::drawPeaks(QPainter& painter, const WaveformBlock& block, int x, int height)
{
    painter.setPen(QPen(m_config.peakColor, 1));
    painter.setBrush(QBrush(m_config.peakColor));

    int centerY = height / 2;

    // Calcular posiciones Y usando todo el espacio
    int minY = centerY - scaleValue(block.minValue, height);
    int maxY = centerY - scaleValue(block.maxValue, height);

    // Limitar a los bordes del widget
    minY = std::max(0, std::min(height - 1, minY));
    maxY = std::max(0, std::min(height - 1, maxY));

    // Dibujar línea de pico a pico
    painter.drawLine(x + m_config.blockWidth / 2, minY,
                     x + m_config.blockWidth / 2, maxY);

    // Dibujar rectángulo representativo
    if (std::abs(maxY - minY) > 1) {
        painter.drawRect(x, std::min(minY, maxY),
                         m_config.blockWidth, std::abs(maxY - minY));
    }
}

void WaveformRenderer::drawRMS(QPainter& painter, const WaveformBlock& block, int x, int height)
{
    painter.setPen(QPen(m_config.rmsColor, 2));

    int centerY = height / 2;
    int rmsHeight = scaleValue(block.rmsValue, height);

    // Calcular posiciones Y y limitar a los bordes
    int rmsTop = std::max(0, centerY - rmsHeight);
    int rmsBottom = std::min(height - 1, centerY + rmsHeight);

    // Dibujar líneas RMS positiva y negativa
    painter.drawLine(x, rmsTop, x + m_config.blockWidth, rmsTop);
    painter.drawLine(x, rmsBottom, x + m_config.blockWidth, rmsBottom);
}

void WaveformRenderer::updateVisibleRange()
{
    int width = rect().width();
    int blockTotalWidth = m_config.blockWidth + m_config.blockSpacing;
    int maxVisibleBlocks = width / blockTotalWidth;

    if (m_config.scrolling) {
        // Modo scrolling: mostrar los últimos bloques
        m_visibleEndIndex = m_blocks.size();
        m_visibleStartIndex = std::max(0, m_visibleEndIndex - maxVisibleBlocks);
    } else {
        // Modo fijo: usar offset manual
        m_visibleStartIndex = std::max(0, m_scrollOffset);
        m_visibleEndIndex = qMin(m_blocks.size(),
                                 static_cast<qsizetype>(m_visibleStartIndex + maxVisibleBlocks));

    }
}

float WaveformRenderer::scaleValue(float value, int height) const
{
    float scale = m_config.autoScale ?
                      (1.0f / (m_maxAmplitude * m_zoom)) :
                      m_config.manualScale * m_zoom;

    // Usar todo el espacio disponible (sin el margen 0.8f)
    return value * scale * (height / 2.0f);
}

int WaveformRenderer::getBlockAtPosition(int x) const
{
    int blockTotalWidth = m_config.blockWidth + m_config.blockSpacing;
    int blockIndex = x / blockTotalWidth;

    if (m_config.scrolling) {
        int width = rect().width();
        int maxVisibleBlocks = width / blockTotalWidth;
        int startIndex = std::max(0, static_cast<int>(m_blocks.size()) - maxVisibleBlocks);
        return startIndex + blockIndex;
    } else {
        return m_visibleStartIndex + blockIndex;
    }
}
