#include "include/waveformview.h"
#include "qdatetime.h"
#include <QPaintEvent>
#include <QApplication>
#include <QtMath>
#include <QDebug>
#include <algorithm>

WaveformView::WaveformView(QWidget *parent)
    : QWidget(parent)
    , m_displayMode(Mono)
    , m_renderStyle(Filled)
    , m_waveformColor(QColor(100, 150, 255))
    , m_backgroundColor(QColor(30, 30, 30))
    , m_gridColor(QColor(60, 60, 60))
    , m_selectionColor(QColor(255, 255, 0, 80))
    , m_playbackCursorColor(QColor(255, 100, 100))
    , m_zoomLevel(1.0)
    , m_scrollPosition(0.0)
    , m_maxScrollPosition(0.0)
    , m_playbackPosition(0.0)
    , m_showPlaybackCursor(true)
    , m_hasSelection(false)
    , m_selectionStart(0.0)
    , m_selectionEnd(0.0)
    , m_showGrid(true)
    , m_showTimeMarkers(true)
    , m_showAmplitudeMarkers(true)
    , m_mouseMode(None)
    , m_needsDataUpdate(false)
    , m_lastWidth(0)
{
    // Configurar widget
    setMinimumSize(200, 100);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    // Colores por defecto para canales
    m_channelColors = {
        QColor(100, 150, 255), // Azul
        QColor(255, 150, 100), // Naranja
        QColor(150, 255, 100), // Verde
        QColor(255, 100, 150), // Rosa
        QColor(150, 100, 255), // Púrpura
        QColor(255, 255, 100)  // Amarillo
    };

    // Timer para actualizaciones diferidas
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    m_updateTimer->setInterval(50); // 50ms de delay
    connect(m_updateTimer, &QTimer::timeout, this, &WaveformView::updateWaveformData);
}

void WaveformView::setAudioData(const FileAudioData &audio) {
    m_audioData = audio;
    m_needsDataUpdate = true;

    // Reset navegación
    m_scrollPosition = 0.0;
    m_playbackPosition = 0.0;
    clearSelection();

    // Actualizar datos de forma de onda
    if (!m_updateTimer->isActive()) {
        m_updateTimer->start();
    }

    update();
}

void WaveformView::clearAudioData() {
    m_audioData.clear();
    m_waveformData.clear();
    m_monoWaveformData.clear();
    m_needsDataUpdate = false;

    clearSelection();
    m_scrollPosition = 0.0;
    m_playbackPosition = 0.0;

    update();
}

void WaveformView::updateWaveformData() {
    if (!hasAudioData()) {
        m_waveformData.clear();
        m_monoWaveformData.clear();
        m_needsDataUpdate = false;
        update();
        return;
    }

    calculateWaveformData();
    updateScrollBounds();
    m_needsDataUpdate = false;
    update();
}

void WaveformView::calculateWaveformData() {
    const int widgetWidth = width();
    if (widgetWidth <= 0) return;

    // Calcular número de bloques necesarios basado en zoom y ancho
    const int targetBlocks = static_cast<int>(widgetWidth * m_zoomLevel);

    // Limpiar datos previos
    m_waveformData.clear();
    m_waveformData.resize(m_audioData.channelCount());

    // Procesar cada canal
    for (int c = 0; c < m_audioData.channelCount(); ++c) {
        const QVector<float>& channelData = m_audioData.getChannel(c);
        if (!channelData.isEmpty()) {
            m_waveformData[c] = downsampleChannel(channelData, targetBlocks);
        }
    }

    // Crear datos mono si es necesario
    if (m_displayMode == Mono && m_audioData.channelCount() > 1) {
        QVector<float> monoData = m_audioData.toMono();
        m_monoWaveformData = downsampleChannel(monoData, targetBlocks);
    } else if (m_displayMode == Mono && m_audioData.channelCount() == 1) {
        m_monoWaveformData = m_waveformData[0];
    }
}

WaveformData WaveformView::downsampleChannel(const QVector<float> &samples, int targetBlocks) const {
    WaveformData result;

    if (samples.isEmpty() || targetBlocks <= 0) {
        return result;
    }

    const int sampleCount = samples.size();
    const int samplesPerBlock = qMax(1, sampleCount / targetBlocks);
    const int actualBlocks = (sampleCount + samplesPerBlock - 1) / samplesPerBlock;

    result.minValues.reserve(actualBlocks);
    result.maxValues.reserve(actualBlocks);
    result.samplesPerBlock = samplesPerBlock;
    result.originalSampleCount = sampleCount;

    for (int block = 0; block < actualBlocks; ++block) {
        const int startIdx = block * samplesPerBlock;
        const int endIdx = qMin(startIdx + samplesPerBlock, sampleCount);

        float minVal = samples[startIdx];
        float maxVal = samples[startIdx];

        for (int i = startIdx + 1; i < endIdx; ++i) {
            const float sample = samples[i];
            minVal = qMin(minVal, sample);
            maxVal = qMax(maxVal, sample);
        }

        result.minValues.append(minVal);
        result.maxValues.append(maxVal);
    }

    return result;
}

void WaveformView::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Verificar si necesitamos actualizar datos
    if (m_needsDataUpdate || width() != m_lastWidth) {
        m_lastWidth = width();
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }

    // Dibujar elementos
    drawBackground(painter);

    if (hasAudioData()) {
        if (m_showGrid) {
            drawGrid(painter);
        }

        drawWaveform(painter);

        if (m_hasSelection) {
            drawSelection(painter);
        }

        if (m_showPlaybackCursor) {
            drawPlaybackCursor(painter);
        }

        if (m_showTimeMarkers) {
            drawTimeMarkers(painter);
        }
    }
}

void WaveformView::drawBackground(QPainter &painter) {
    painter.fillRect(rect(), m_backgroundColor);
}

void WaveformView::drawGrid(QPainter &painter) {
    if (!hasAudioData()) return;

    painter.setPen(QPen(m_gridColor, 1, Qt::DotLine));

    const QRect drawRect = rect();
    const int channelCount = getVisibleChannelCount();

    // Líneas horizontales (separadores de canal y centro)
    for (int c = 0; c < channelCount; ++c) {
        QRect channelRect = getChannelRect(c);

        // Línea central del canal
        int centerY = channelRect.center().y();
        painter.drawLine(drawRect.left(), centerY, drawRect.right(), centerY);

        // Líneas de separación
        if (c > 0) {
            painter.drawLine(drawRect.left(), channelRect.top(),
                             drawRect.right(), channelRect.top());
        }
    }

    // Líneas verticales (marcadores de tiempo)
    const double duration = audioDurationSeconds();
    if (duration > 0) {
        const double visibleDuration = duration / m_zoomLevel;
        const double startTime = m_scrollPosition * (duration - visibleDuration);

        // Determinar intervalo de grid apropiado
        double gridInterval = 1.0; // segundos
        if (visibleDuration < 10) gridInterval = 1.0;
        else if (visibleDuration < 60) gridInterval = 5.0;
        else if (visibleDuration < 300) gridInterval = 30.0;
        else gridInterval = 60.0;

        const double firstGrid = qCeil(startTime / gridInterval) * gridInterval;

        for (double time = firstGrid; time < startTime + visibleDuration; time += gridInterval) {
            int x = timeToPixel(time / duration);
            if (x >= drawRect.left() && x <= drawRect.right()) {
                painter.drawLine(x, drawRect.top(), x, drawRect.bottom());
            }
        }
    }
}

void WaveformView::drawWaveform(QPainter &painter) {
    if (!hasAudioData()) return;

    const int channelCount = getVisibleChannelCount();

    for (int c = 0; c < channelCount; ++c) {
        QRect channelRect = getChannelRect(c);
        drawChannel(painter, c, channelRect);
    }
}

void WaveformView::drawChannel(QPainter &painter, int channelIndex, const QRect &channelRect) {
    const WaveformData* data = nullptr;
    QColor channelColor;

    // Seleccionar datos y color
    if (m_displayMode == Mono) {
        data = &m_monoWaveformData;
        channelColor = m_waveformColor;
    } else {
        if (channelIndex >= m_waveformData.size()) return;
        data = &m_waveformData[channelIndex];
        channelColor = (channelIndex < m_channelColors.size()) ?
                           m_channelColors[channelIndex] : m_waveformColor;
    }

    if (!data || data->isEmpty()) return;

    const int centerY = channelRect.center().y();
    const int maxHeight = channelRect.height() / 2 - CHANNEL_MARGIN;

    painter.setPen(QPen(channelColor, 1));
    painter.setBrush(QBrush(channelColor));

    // Calcular rango visible
    const double totalDuration = audioDurationSeconds();
    const double visibleDuration = totalDuration / m_zoomLevel;
    const double startTime = m_scrollPosition * (totalDuration - visibleDuration);
    const double endTime = startTime + visibleDuration;

    const int totalBlocks = data->blockCount();
    const int startBlock = qMax(0, static_cast<int>((startTime / totalDuration) * totalBlocks));
    const int endBlock = qMin(totalBlocks, static_cast<int>((endTime / totalDuration) * totalBlocks) + 1);

    if (startBlock >= endBlock) return;

    // Dibujar según el estilo
    switch (m_renderStyle) {
    case Line: {
        QVector<QPoint> points;
        points.reserve((endBlock - startBlock) * 2);

        for (int i = startBlock; i < endBlock; ++i) {
            const double blockTime = (static_cast<double>(i) / totalBlocks) * totalDuration;
            const int x = timeToPixel(blockTime / totalDuration);

            const int minY = centerY - static_cast<int>(data->minValues[i] * maxHeight);
            const int maxY = centerY - static_cast<int>(data->maxValues[i] * maxHeight);

            points.append(QPoint(x, maxY));
            points.append(QPoint(x, minY));
        }

        if (!points.isEmpty()) {
            painter.drawPolyline(points);
        }
        break;
    }

    case Filled: {
        QVector<QPoint> topPoints, bottomPoints;
        topPoints.reserve(endBlock - startBlock);
        bottomPoints.reserve(endBlock - startBlock);

        for (int i = startBlock; i < endBlock; ++i) {
            const double blockTime = (static_cast<double>(i) / totalBlocks) * totalDuration;
            const int x = timeToPixel(blockTime / totalDuration);

            const int minY = centerY - static_cast<int>(data->minValues[i] * maxHeight);
            const int maxY = centerY - static_cast<int>(data->maxValues[i] * maxHeight);

            topPoints.append(QPoint(x, maxY));
            bottomPoints.prepend(QPoint(x, minY)); // Invertir orden para cerrar polígono
        }

        if (!topPoints.isEmpty()) {
            QVector<QPoint> polygon = topPoints + bottomPoints;
            painter.drawPolygon(polygon);
        }
        break;
    }

    case Outline: {
        painter.setBrush(Qt::NoBrush);

        for (int i = startBlock; i < endBlock; ++i) {
            const double blockTime = (static_cast<double>(i) / totalBlocks) * totalDuration;
            const int x = timeToPixel(blockTime / totalDuration);

            const int minY = centerY - static_cast<int>(data->minValues[i] * maxHeight);
            const int maxY = centerY - static_cast<int>(data->maxValues[i] * maxHeight);

            painter.drawLine(x, minY, x, maxY);
        }
        break;
    }
    }

    // Dibujar marcadores de amplitud si están habilitados
    if (m_showAmplitudeMarkers) {
        drawAmplitudeMarkers(painter, channelRect);
    }
}

void WaveformView::drawSelection(QPainter &painter) {
    if (!m_hasSelection || !hasAudioData()) return;

    const int startX = timeToPixel(m_selectionStart);
    const int endX = timeToPixel(m_selectionEnd);
    const int width = endX - startX;

    if (width > 0) {
        QRect selectionRect(startX, 0, width, height());
        painter.fillRect(selectionRect, m_selectionColor);

        // Bordes de selección
        painter.setPen(QPen(m_selectionColor.darker(), 2));
        painter.drawLine(startX, 0, startX, height());
        painter.drawLine(endX, 0, endX, height());
    }
}

void WaveformView::drawPlaybackCursor(QPainter &painter) {
    if (!hasAudioData()) return;

    const int x = timeToPixel(m_playbackPosition);

    painter.setPen(QPen(m_playbackCursorColor, 2));
    painter.drawLine(x, 0, x, height());

    // Triángulo en la parte superior
    const int triangleSize = 8;
    QPolygon triangle;
    triangle << QPoint(x, 0)
             << QPoint(x - triangleSize/2, triangleSize)
             << QPoint(x + triangleSize/2, triangleSize);

    painter.setBrush(m_playbackCursorColor);
    painter.drawPolygon(triangle);
}

void WaveformView::drawTimeMarkers(QPainter &painter) {
    if (!hasAudioData()) return;

    const double duration = audioDurationSeconds();
    const double visibleDuration = duration / m_zoomLevel;
    const double startTime = m_scrollPosition * (duration - visibleDuration);

    // Determinar formato y intervalo
    QString timeFormat = "mm:ss";
    double interval = 10.0;

    if (visibleDuration < 60) {
        interval = 10.0;
        timeFormat = "ss.z";
    } else if (visibleDuration < 600) {
        interval = 30.0;
        timeFormat = "mm:ss";
    } else {
        interval = 60.0;
        timeFormat = "mm:ss";
    }

    painter.setPen(QPen(Qt::white, 1));
    painter.setFont(QFont("Arial", 9));

    const double firstMarker = qCeil(startTime / interval) * interval;

    for (double time = firstMarker; time < startTime + visibleDuration; time += interval) {
        const int x = timeToPixel(time / duration);

        if (x >= 0 && x <= width()) {
            // Línea del marcador
            painter.drawLine(x, height() - TIME_MARKER_HEIGHT, x, height());

            // Texto del tiempo
            QTime timeObj = QTime(0, 0).addMSecs(static_cast<int>(time * 1000));
            QString timeText = timeObj.toString(timeFormat);

            QRect textRect(x - 30, height() - TIME_MARKER_HEIGHT, 60, TIME_MARKER_HEIGHT);
            painter.drawText(textRect, Qt::AlignCenter, timeText);
        }
    }
}

void WaveformView::drawAmplitudeMarkers(QPainter &painter, const QRect &channelRect) {
    painter.setPen(QPen(Qt::lightGray, 1));
    painter.setFont(QFont("Arial", 8));

    const int centerY = channelRect.center().y();
    const int maxHeight = channelRect.height() / 2 - CHANNEL_MARGIN;

    // Marcadores de amplitud
    const QVector<float> amplitudes = {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f};

    for (float amp : amplitudes) {
        const int y = centerY - static_cast<int>(amp * maxHeight);

        if (y >= channelRect.top() && y <= channelRect.bottom()) {
            // Línea corta
            painter.drawLine(0, y, 10, y);

            // Texto
            QString ampText = QString::number(amp, 'f', 1);
            QRect textRect(0, y - 8, AMPLITUDE_MARKER_WIDTH, 16);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, ampText);
        }
    }
}

// Métodos de configuración
void WaveformView::setDisplayMode(DisplayMode mode) {
    if (m_displayMode != mode) {
        m_displayMode = mode;
        m_needsDataUpdate = true;
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }
}

void WaveformView::setRenderStyle(RenderStyle style) {
    if (m_renderStyle != style) {
        m_renderStyle = style;
        update();
    }
}

void WaveformView::setWaveformColor(const QColor &color) {
    m_waveformColor = color;
    update();
}

void WaveformView::setChannelColor(int channel, const QColor &color) {
    if (channel >= 0) {
        while (m_channelColors.size() <= channel) {
            m_channelColors.append(m_waveformColor);
        }
        m_channelColors[channel] = color;
        update();
    }
}

void WaveformView::setBackgroundColor(const QColor &color) {
    m_backgroundColor = color;
    update();
}

void WaveformView::setGridColor(const QColor &color) {
    m_gridColor = color;
    update();
}

void WaveformView::setZoomLevel(double zoom) {
    zoom = qBound(1.0, zoom, 100.0);
    if (m_zoomLevel != zoom) {
        m_zoomLevel = zoom;
        updateScrollBounds();
        clampScrollPosition();
        m_needsDataUpdate = true;

        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }

        emit zoomChanged(m_zoomLevel);
    }
}

void WaveformView::setScrollPosition(double position) {
    position = qBound(0.0, position, m_maxScrollPosition);
    if (m_scrollPosition != position) {
        m_scrollPosition = position;
        update();
        emit scrollChanged(m_scrollPosition);
    }
}

void WaveformView::setPlaybackPosition(double position) {
    position = qBound(0.0, position, 1.0);
    if (m_playbackPosition != position) {
        m_playbackPosition = position;
        update();
    }
}

void WaveformView::setShowPlaybackCursor(bool show) {
    if (m_showPlaybackCursor != show) {
        m_showPlaybackCursor = show;
        update();
    }
}

void WaveformView::setSelection(double start, double end) {
    start = qBound(0.0, start, 1.0);
    end = qBound(0.0, end, 1.0);

    if (start > end) {
        qSwap(start, end);
    }

    if (m_selectionStart != start || m_selectionEnd != end || !m_hasSelection) {
        m_selectionStart = start;
        m_selectionEnd = end;
        m_hasSelection = true;
        update();
        emit selectionChanged(start, end);
    }
}

void WaveformView::clearSelection() {
    if (m_hasSelection) {
        m_hasSelection = false;
        m_selectionStart = 0.0;
        m_selectionEnd = 0.0;
        update();
        emit selectionChanged(0.0, 0.0);
    }
}

QPair<double, double> WaveformView::selection() const {
    return QPair<double, double>(m_selectionStart, m_selectionEnd);
}

void WaveformView::setShowGrid(bool show) {
    if (m_showGrid != show) {
        m_showGrid = show;
        update();
    }
}

void WaveformView::setShowTimeMarkers(bool show) {
    if (m_showTimeMarkers != show) {
        m_showTimeMarkers = show;
        update();
    }
}

void WaveformView::setShowAmplitudeMarkers(bool show) {
    if (m_showAmplitudeMarkers != show) {
        m_showAmplitudeMarkers = show;
        update();
    }
}

double WaveformView::audioDurationSeconds() const {
    return hasAudioData() ? m_audioData.durationSeconds() : 0.0;
}

int WaveformView::audioSampleRate() const {
    return hasAudioData() ? m_audioData.sampleRate : 0;
}

double WaveformView::pixelToTime(int pixel) const {
    if (!hasAudioData() || width() <= 0) return 0.0;

    const double duration = audioDurationSeconds();
    const double visibleDuration = duration / m_zoomLevel;
    const double startTime = m_scrollPosition * (duration - visibleDuration);

    return startTime + (static_cast<double>(pixel) / width()) * visibleDuration;
}

int WaveformView::timeToPixel(double time) const {
    if (!hasAudioData() || width() <= 0) return 0;

    const double duration = audioDurationSeconds();
    const double visibleDuration = duration / m_zoomLevel;
    const double startTime = m_scrollPosition * (duration - visibleDuration);

    return static_cast<int>(((time - startTime) / visibleDuration) * width());
}

double WaveformView::pixelToAmplitude(int pixel, int channelHeight) const {
    if (channelHeight <= 0) return 0.0;

    const int maxHeight = channelHeight / 2 - CHANNEL_MARGIN;
    const int centerY = channelHeight / 2;

    return static_cast<double>(centerY - pixel) / maxHeight;
}

int WaveformView::amplitudeToPixel(double amplitude, int channelHeight) const {
    if (channelHeight <= 0) return 0;

    const int maxHeight = channelHeight / 2 - CHANNEL_MARGIN;
    const int centerY = channelHeight / 2;

    return centerY - static_cast<int>(amplitude * maxHeight);
}

// Eventos del mouse
void WaveformView::mousePressEvent(QMouseEvent *event) {
    if (!hasAudioData()) return;

    if (event->button() == Qt::LeftButton) {
        m_mouseStartPos = event->pos();
        m_mouseMode = Selecting;

        const double clickTime = pixelToTime(event->x()) / audioDurationSeconds();
        m_selectionStartTime = qBound(0.0, clickTime, 1.0);

        clearSelection();
        setFocus();
    } else if (event->button() == Qt::RightButton) {
        // Click derecho para posicionar cursor de reproducción
        const double clickTime = pixelToTime(event->x()) / audioDurationSeconds();
        emit playbackPositionClicked(qBound(0.0, clickTime, 1.0));
    }

    event->accept();
}

void WaveformView::mouseMoveEvent(QMouseEvent *event) {
    if (!hasAudioData()) return;

    if (m_mouseMode == Selecting && (event->buttons() & Qt::LeftButton)) {
        const double currentTime = pixelToTime(event->x()) / audioDurationSeconds();
        const double clampedTime = qBound(0.0, currentTime, 1.0);

        const double start = qMin(m_selectionStartTime, clampedTime);
        const double end = qMax(m_selectionStartTime, clampedTime);

        if (qAbs(end - start) > 0.001) { // Selección mínima de 1ms
            setSelection(start, end);
        }
    } else if (m_mouseMode == Dragging && (event->buttons() & Qt::MiddleButton)) {
        // Arrastrar para hacer scroll
        const int deltaX = event->x() - m_mouseStartPos.x();
        const double duration = audioDurationSeconds();
        const double visibleDuration = duration / m_zoomLevel;
        const double deltaTime = (static_cast<double>(deltaX) / width()) * visibleDuration;
        const double newScrollPos = m_initialScrollPosition - (deltaTime / (duration - visibleDuration));

        setScrollPosition(newScrollPos);
    }

    event->accept();
}

void WaveformView::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_mouseMode == Selecting) {
        m_mouseMode = None;
    } else if (event->button() == Qt::MiddleButton && m_mouseMode == Dragging) {
        m_mouseMode = None;
    }

    event->accept();
}

void WaveformView::wheelEvent(QWheelEvent *event) {
    if (!hasAudioData()) return;

    const QPoint numDegrees = event->angleDelta() / 8;
    const QPoint numSteps = numDegrees / 15;

    if (!numSteps.isNull()) {
        if (event->modifiers() & Qt::ControlModifier) {
            // Zoom con Ctrl + rueda
            const double zoomFactor = 1.0 + (numSteps.y() * 0.1);
            const double newZoom = m_zoomLevel * zoomFactor;
            setZoomLevel(newZoom);
        } else {
            // Scroll horizontal
            const double duration = audioDurationSeconds();
            const double visibleDuration = duration / m_zoomLevel;
            const double scrollDelta = (numSteps.y() * 0.1) * (visibleDuration / duration);
            setScrollPosition(m_scrollPosition - scrollDelta);
        }
    }

    event->accept();
}

void WaveformView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);

    if (!hasAudioData()) return;

    // Recalcular datos de waveform si el ancho cambió significativamente
    const int newWidth = event->size().width();
    const int oldWidth = event->oldSize().width();

    if (qAbs(newWidth - oldWidth) > 10) {
        m_needsDataUpdate = true;
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    }

    updateScrollBounds();
    clampScrollPosition();
}

// Métodos auxiliares
QRect WaveformView::getChannelRect(int channelIndex) const {
    const int channelCount = getVisibleChannelCount();
    if (channelCount <= 0 || channelIndex < 0 || channelIndex >= channelCount) {
        return QRect();
    }

    const int totalHeight = height() - (m_showTimeMarkers ? TIME_MARKER_HEIGHT : 0);
    const int channelHeight = totalHeight / channelCount;
    const int y = channelIndex * channelHeight;

    return QRect(m_showAmplitudeMarkers ? AMPLITUDE_MARKER_WIDTH : 0,
                 y,
                 width() - (m_showAmplitudeMarkers ? AMPLITUDE_MARKER_WIDTH : 0),
                 channelHeight);
}

int WaveformView::getVisibleChannelCount() const {
    if (!hasAudioData()) return 0;

    switch (m_displayMode) {
    case Mono:
        return 1;
    case Stereo:
        return qMin(2, m_audioData.channelCount());
    case AllChannels:
        return m_audioData.channelCount();
    }

    return 1;
}

void WaveformView::updateScrollBounds() {
    if (!hasAudioData()) {
        m_maxScrollPosition = 0.0;
        return;
    }

    const double duration = audioDurationSeconds();
    const double visibleDuration = duration / m_zoomLevel;

    if (visibleDuration >= duration) {
        m_maxScrollPosition = 0.0;
    } else {
        m_maxScrollPosition = 1.0 - (visibleDuration / duration);
    }
}

void WaveformView::clampScrollPosition() {
    m_scrollPosition = qBound(0.0, m_scrollPosition, m_maxScrollPosition);
}

void WaveformView::handleMousePress(QMouseEvent *event) {
    // Método auxiliar para manejo de mouse más complejo si es necesario
    // Por ahora, redirige a mousePressEvent
    mousePressEvent(event);
}

void WaveformView::handleMouseMove(QMouseEvent *event) {
    // Método auxiliar para manejo de mouse más complejo si es necesario
    // Por ahora, redirige a mouseMoveEvent
    mouseMoveEvent(event);
}

void WaveformView::handleMouseRelease(QMouseEvent *event) {
    // Método auxiliar para manejo de mouse más complejo si es necesario
    // Por ahora, redirige a mouseReleaseEvent
    mouseReleaseEvent(event);
}
