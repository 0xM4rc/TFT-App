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

    // Configurar colores y configuración inicial para waveform estilo Audacity
    initializeForAudacityStyle();

    qDebug() << "WaveformRenderer inicializado con estilo Audacity";
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
    setAutoFillBackground(true);

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
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // 1) Fondo completo estilo Audacity
    QRect fullRect = rect();
    drawAudacityBackground(painter, fullRect);

    QMutexLocker locker(&m_mutex);

    // 2) Si no hay datos, mostramos mensaje y salimos
    if (m_blocks.isEmpty()) {
        painter.setPen(QColor(180, 180, 180));
        QFont f = painter.font();
        f.setPointSize(10);
        painter.setFont(f);
        painter.drawText(fullRect, Qt::AlignCenter,
                         tr("Esperando datos de audio..."));
        return;
    }

    // 3) Definir margen izquierdo para la escala lineal
    const int marginLeft = 40;
    int fullW = fullRect.width();
    int fullH = fullRect.height();
    int wfW   = fullW - marginLeft;
    int wfH   = fullH;
    QRect waveformRect(fullRect.left() + marginLeft,
                       fullRect.top(),
                       wfW, wfH);

    // 4) Dibujar escala vertical lineal
    painter.setPen(Qt::white);
    QFontMetrics fm(painter.font());
    int ticks = 4;  // 5 líneas: +1, +0.5, 0, -0.5, -1
    for (int i = 0; i <= ticks; ++i) {
        float norm = 1.0f - 2.0f * (float(i) / ticks);  // de 1 a -1
        int y = waveformRect.top()
                + int(((1.0f - norm) / 2.0f) * wfH);
        // Marca pequeña
        painter.drawLine(waveformRect.left() - 5, y,
                         waveformRect.left() - 1, y);
        // Etiqueta lineal
        QString linLabel = QString::number(norm, 'f', 1);
        painter.drawText(QRect(0, y - fm.height()/2,
                               marginLeft - 8, fm.height()),
                         Qt::AlignRight, linLabel);
    }

    // 5) Dibujar la waveform en el área restante
    painter.save();
    painter.translate(waveformRect.topLeft());
    painter.setClipRect(0, 0, wfW, wfH);
    drawAudacityWaveform(painter, wfH, wfW);
    painter.restore();

    // 6) Línea central horizontal
    painter.setPen(QPen(QColor(100,100,100), 1));
    int centerY = waveformRect.top() + wfH / 2;
    painter.drawLine(waveformRect.left(),  centerY,
                     waveformRect.right(), centerY);

    // 7) Información de estado
    drawStatusInfo(painter, fullRect);
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

void WaveformRenderer::initializeForAudacityStyle()
{
    // Configuración optimizada para estilo Audacity
    m_config.maxVisibleBlocks = 400;
    m_config.blockWidth = 1;
    m_config.blockSpacing = 0;

    // Colores estilo Audacity
    m_config.peakColor = QColor(100, 149, 237);      // Azul claro (CornflowerBlue)
    m_config.rmsColor = QColor(70, 130, 180);        // Azul acero
    m_config.backgroundColor = QColor(60, 60, 60);    // Gris oscuro
    m_config.showPeaks = true;
    m_config.showRMS = false;
    m_config.autoScale = true;
    m_config.manualScale = 1.0f;
    m_config.scrolling = true;
    m_config.updateInterval = 30;

    // Actualizar colores del widget
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, m_config.backgroundColor);
    setPalette(palette);
    setAutoFillBackground(true);
}

void WaveformRenderer::drawAudacityBackground(QPainter& painter, const QRect& rect)
{
    // Fondo con gradiente sutil como Audacity
    QLinearGradient gradient(0, 0, 0, rect.height());
    gradient.setColorAt(0, QColor(65, 65, 65));
    gradient.setColorAt(0.5, QColor(60, 60, 60));
    gradient.setColorAt(1, QColor(55, 55, 55));

    painter.fillRect(rect, gradient);

    // Líneas de cuadrícula sutiles
    painter.setPen(QPen(QColor(70, 70, 70), 1));

    // Líneas horizontales cada 25% de altura
    for (int i = 1; i < 4; i++) {
        int y = rect.height() * i / 4;
        painter.drawLine(rect.left(), y, rect.right(), y);
    }
}

void WaveformRenderer::drawAudacityWaveform(QPainter& painter, int height, int width)
{
    if (m_blocks.isEmpty()) return;

    // Configurar antialiasing para suavidad
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Calcular cuántos bloques por pixel
    float blocksPerPixel = static_cast<float>(m_blocks.size()) / width;

    if (blocksPerPixel < 1.0f) {
        // Menos bloques que pixels - usar interpolación suave
        drawAudacityInterpolatedWaveform(painter, height, width);
    } else {
        // Más bloques que pixels - usar densidad con gradiente
        drawAudacityDensityWaveform(painter, height, width, blocksPerPixel);
    }
}

void WaveformRenderer::drawAudacityInterpolatedWaveform(QPainter& painter, int height, int width)
{
    int centerY = height / 2;

    // Crear gradiente para efecto de profundidad
    QLinearGradient waveGradient(0, 0, 0, height);
    waveGradient.setColorAt(0, QColor(120, 169, 255, 200));  // Azul claro en los bordes
    waveGradient.setColorAt(0.5, QColor(100, 149, 237, 255)); // Azul intenso en el centro
    waveGradient.setColorAt(1, QColor(120, 169, 255, 200));  // Azul claro en los bordes

    QBrush waveBrush(waveGradient);
    painter.setBrush(waveBrush);

    // Dibujar como polígono relleno para efecto más suave
    QPolygonF upperWave, lowerWave;

    for (int i = 0; i < m_blocks.size(); i++) {
        float pixelPos = (static_cast<float>(i) / m_blocks.size()) * width;
        int x = static_cast<int>(pixelPos);

        if (x >= width) break;

        const WaveformBlock& block = m_blocks[i];

        // Calcular posiciones Y con suavizado
        int minY = centerY - scaleValue(block.minValue, height);
        int maxY = centerY - scaleValue(block.maxValue, height);

        // Limitar a los bordes
        minY = std::max(0, std::min(height - 1, minY));
        maxY = std::max(0, std::min(height - 1, maxY));

        upperWave << QPointF(x, maxY);
        lowerWave.prepend(QPointF(x, minY));
    }

    // Combinar ambas partes del polígono
    QPolygonF completeWave = upperWave + lowerWave;

    // Dibujar el polígono relleno
    painter.setPen(QPen(QColor(80, 129, 217), 1));
    painter.drawPolygon(completeWave);

    // Dibujar contorno más definido
    painter.setPen(QPen(QColor(100, 149, 237), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(upperWave);
    painter.drawPolyline(lowerWave);
}

void WaveformRenderer::drawAudacityDensityWaveform(QPainter& painter, int height, int width, float blocksPerPixel)
{
    int centerY = height / 2;

    // Crear gradiente vertical para cada línea
    QLinearGradient lineGradient(0, 0, 0, height);
    lineGradient.setColorAt(0, QColor(120, 169, 255, 180));
    lineGradient.setColorAt(0.5, QColor(100, 149, 237, 255));
    lineGradient.setColorAt(1, QColor(120, 169, 255, 180));

    QPen wavePen;
    wavePen.setBrush(QBrush(lineGradient));
    wavePen.setWidth(1);
    wavePen.setCapStyle(Qt::RoundCap);
    painter.setPen(wavePen);

    for (int pixel = 0; pixel < width; pixel++) {
        int startBlock = static_cast<int>(pixel * blocksPerPixel);
        int endBlock = static_cast<int>((pixel + 1) * blocksPerPixel);

        if (startBlock >= m_blocks.size()) break;
        endBlock = std::min(endBlock, static_cast<int>(m_blocks.size()));

        // Encontrar min/max en este rango de bloques
        float minVal = m_blocks[startBlock].minValue;
        float maxVal = m_blocks[startBlock].maxValue;
        float avgVal = 0.0f;
        int count = 0;

        for (int b = startBlock; b < endBlock; b++) {
            minVal = std::min(minVal, m_blocks[b].minValue);
            maxVal = std::max(maxVal, m_blocks[b].maxValue);
            avgVal += (m_blocks[b].minValue + m_blocks[b].maxValue) / 2.0f;
            count++;
        }

        if (count > 0) {
            avgVal /= count;
        }

        // Dibujar línea vertical con intensidad basada en densidad
        int minY = centerY - scaleValue(minVal, height);
        int maxY = centerY - scaleValue(maxVal, height);

        minY = std::max(0, std::min(height - 1, minY));
        maxY = std::max(0, std::min(height - 1, maxY));

        // Ajustar opacidad según la densidad
        float density = std::min(1.0f, blocksPerPixel / 10.0f);
        QColor lineColor = QColor(100, 149, 237);
        lineColor.setAlphaF(0.7f + density * 0.3f);

        painter.setPen(QPen(lineColor, 1));
        painter.drawLine(pixel, minY, pixel, maxY);
    }
}

void WaveformRenderer::drawStatusInfo(QPainter& painter, const QRect& rect)
{
    // Fondo semitransparente para el texto
    QRect statusRect(rect.left() + 5, rect.top() + 5, 250, 20);
    painter.fillRect(statusRect, QColor(0, 0, 0, 100));

    // Texto con estilo Audacity
    painter.setPen(QColor(200, 200, 200));
    QFont font = painter.font();
    font.setPointSize(9);
    font.setFamily("Arial");
    painter.setFont(font);

    QString statusText = QString("Bloques: %1 | Zoom: %2x | Amplitud: %3")
                             .arg(m_blocks.size())
                             .arg(m_zoom, 0, 'f', 1)
                             .arg(m_maxAmplitude, 0, 'f', 3);

    painter.drawText(statusRect, Qt::AlignLeft | Qt::AlignVCenter, statusText);
}

void WaveformRenderer::drawPixelDensityWaveform(QPainter& painter, int height, int width)
{
    // Redirigir al método específico de Audacity
    drawAudacityWaveform(painter, height, width);
}

void WaveformRenderer::drawInterpolatedWaveform(QPainter& painter, int height, int width)
{
    // Redirigir al método específico de Audacity
    drawAudacityInterpolatedWaveform(painter, height, width);
}

void WaveformRenderer::drawDensityWaveform(QPainter& painter, int height, int width, float blocksPerPixel)
{
    // Redirigir al método específico de Audacity
    drawAudacityDensityWaveform(painter, height, width, blocksPerPixel);
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

void WaveformRenderer::updateVisibleRange()
{
    int width = rect().width();
    int maxVisibleBlocks = width; // Un bloque por pixel en modo continuo

    if (m_config.scrolling) {
        // Modo scrolling: mostrar los últimos bloques
        m_visibleEndIndex = m_blocks.size();
        m_visibleStartIndex = std::max(0, m_visibleEndIndex - maxVisibleBlocks);
    } else {
        // Modo fijo: usar offset manual
        m_visibleStartIndex = std::max(0, m_scrollOffset);
        m_visibleEndIndex = std::min(m_blocks.size(),
                                     static_cast<qsizetype>(m_visibleStartIndex + maxVisibleBlocks));
    }
}

float WaveformRenderer::scaleValue(float value, int height) const
{
    float scale = m_config.autoScale ?
                      (1.0f / (m_maxAmplitude * m_zoom)) :
                      m_config.manualScale * m_zoom;

    // Usar todo el espacio disponible con un poco de margen
    return value * scale * (height / 2.2f);
}

int WaveformRenderer::getBlockAtPosition(int x) const
{
    // En modo continuo, la posición X se mapea directamente a los bloques
    float blocksPerPixel = static_cast<float>(m_blocks.size()) / rect().width();
    int blockIndex = static_cast<int>(x * blocksPerPixel);

    if (m_config.scrolling) {
        int maxVisibleBlocks = rect().width();
        int startIndex = std::max(0, static_cast<int>(m_blocks.size()) - maxVisibleBlocks);
        return startIndex + blockIndex;
    } else {
        return m_visibleStartIndex + blockIndex;
    }
}
