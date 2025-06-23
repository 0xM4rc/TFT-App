#include "include/waveform_widget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QtMath>
#include <QDebug>

const double WaveformWidget::DEFAULT_DISPLAY_DURATION = 2.0;
const int WaveformWidget::DEFAULT_GRID_DIVISIONS = 10;
const QColor WaveformWidget::DEFAULT_WAVEFORM_COLOR = QColor(0, 255, 0, 180);
const QColor WaveformWidget::DEFAULT_BACKGROUND_COLOR = QColor(20, 20, 30);
const QColor WaveformWidget::DEFAULT_GRID_COLOR = QColor(60, 60, 80, 100);

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
    , m_displayDuration(DEFAULT_DISPLAY_DURATION)
    , m_sampleRate(44100)
    , m_channels(1)
    , m_autoScale(true)
    , m_showGrid(true)
    , m_showPeakMarkers(true)
    , m_showRMSLine(true)
    , m_showZeroLine(true)
    , m_amplitudeScale(1.0)
    , m_verticalOffset(0.0)
    , m_gridDivisions(DEFAULT_GRID_DIVISIONS)
    , m_waveformColor(DEFAULT_WAVEFORM_COLOR)
    , m_backgroundColor(DEFAULT_BACKGROUND_COLOR)
    , m_gridColor(DEFAULT_GRID_COLOR)
    , m_renderMode(RenderMode::Line)
    , m_smoothing(true)
    , m_peakLevel(0.0)
    , m_rmsLevel(0.0)
    , m_lastUpdateTime(0)
    , m_frameRate(60.0)
    , m_maxFrameTime(1000.0 / 60.0)
    , m_isMouseTracking(false) // 60 FPS
{
    setMinimumSize(400, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    // Configurar colores adicionales
    m_peakMarkerColor = QColor(255, 100, 100, 200);
    m_rmsLineColor = QColor(255, 255, 100, 150);
    m_zeroLineColor = QColor(128, 128, 128, 100);
    m_textColor = QColor(200, 200, 200);

    // Inicializar fuente
    m_font = QFont("Consolas", 9);
    m_font.setStyleHint(QFont::Monospace);

    // Timer para actualización suave
    m_updateTimer = new QTimer(this);
    m_updateTimer->setSingleShot(true);
    connect(m_updateTimer, &QTimer::timeout, this, [this]() {
        update();
    });

    qDebug() << "WaveformWidget initialized";
}

WaveformWidget::~WaveformWidget()
{
    qDebug() << "WaveformWidget destroyed";
}

void WaveformWidget::updateWaveform(const QVector<float>& waveform)
{
    if (waveform.isEmpty()) {
        return;
    }

    // Control de frame rate
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastUpdateTime < m_maxFrameTime) {
        // Agendar actualización en lugar de actualizar inmediatamente
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start(static_cast<int>(m_maxFrameTime - (currentTime - m_lastUpdateTime)));
        }
        return;
    }

    QMutexLocker locker(&m_dataMutex);

    // Copiar datos nuevos
    m_waveformData = waveform;

    // Calcular estadísticas
    calculateStatistics();

    // Actualizar escala automática si está habilitada
    if (m_autoScale) {
        updateAutoScale();
    }

    m_lastUpdateTime = currentTime;

    // Actualizar widget
    if (!m_updateTimer->isActive()) {
        update();
    }
}

void WaveformWidget::updateWaveform(const QVector<float>& waveform,
                                    double peakLevel,
                                    double rmsLevel)
{
    QMutexLocker locker(&m_dataMutex);

    m_waveformData = waveform;
    m_peakLevel = peakLevel;
    m_rmsLevel = rmsLevel;

    if (m_autoScale) {
        updateAutoScale();
    }

    // Control de frame rate
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    if (currentTime - m_lastUpdateTime >= m_maxFrameTime) {
        m_lastUpdateTime = currentTime;
        update();
    } else if (!m_updateTimer->isActive()) {
        m_updateTimer->start(static_cast<int>(m_maxFrameTime - (currentTime - m_lastUpdateTime)));
    }
}

void WaveformWidget::setDisplayDuration(double seconds)
{
    if (seconds > 0.0 && seconds <= 30.0) {
        m_displayDuration = seconds;
        update();
        qDebug() << "Display duration set to" << seconds << "seconds";
    }
}

void WaveformWidget::setAudioFormat(const QAudioFormat& format)
{
    m_sampleRate = format.sampleRate();
    m_channels = format.channelCount();

    qDebug() << "Audio format updated:" << m_sampleRate << "Hz," << m_channels << "ch";
    update();
}

void WaveformWidget::setAmplitudeScale(double scale)
{
    if (scale > 0.0 && scale <= 10.0) {
        m_amplitudeScale = scale;
        update();
    }
}

void WaveformWidget::setVerticalOffset(double offset)
{
    if (offset >= -1.0 && offset <= 1.0) {
        m_verticalOffset = offset;
        update();
    }
}

void WaveformWidget::setAutoScale(bool enabled)
{
    m_autoScale = enabled;
    if (enabled) {
        updateAutoScale();
    }
    update();
}

void WaveformWidget::setShowGrid(bool show)
{
    m_showGrid = show;
    update();
}

void WaveformWidget::setShowPeakMarkers(bool show)
{
    m_showPeakMarkers = show;
    update();
}

void WaveformWidget::setShowRMSLine(bool show)
{
    m_showRMSLine = show;
    update();
}

void WaveformWidget::setShowZeroLine(bool show)
{
    m_showZeroLine = show;
    update();
}

void WaveformWidget::setWaveformColor(const QColor& color)
{
    m_waveformColor = color;
    update();
}

void WaveformWidget::setBackgroundColor(const QColor& color)
{
    m_backgroundColor = color;
    update();
}

void WaveformWidget::setRenderMode(RenderMode mode)
{
    m_renderMode = mode;
    update();
}

void WaveformWidget::setSmoothing(bool enabled)
{
    m_smoothing = enabled;
    update();
}

void WaveformWidget::setFrameRate(double fps)
{
    if (fps > 0.0 && fps <= 120.0) {
        m_frameRate = fps;
        m_maxFrameTime = 1000.0 / fps;
        qDebug() << "Frame rate set to" << fps << "FPS";
    }
}

void WaveformWidget::clearWaveform()
{
    QMutexLocker locker(&m_dataMutex);
    m_waveformData.clear();
    m_peakLevel = 0.0;
    m_rmsLevel = 0.0;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, m_smoothing);

    // Fondo
    painter.fillRect(rect(), m_backgroundColor);

    // Configurar fuente
    painter.setFont(m_font);

    QMutexLocker locker(&m_dataMutex);

    // Dibujar grilla si está habilitada
    if (m_showGrid) {
        drawGrid(painter);
    }

    // Dibujar línea de cero si está habilitada
    if (m_showZeroLine) {
        drawZeroLine(painter);
    }

    // Dibujar forma de onda
    if (!m_waveformData.isEmpty()) {
        drawWaveform(painter);

        // Dibujar línea RMS si está habilitada
        if (m_showRMSLine && m_rmsLevel > 0.0) {
            drawRMSLine(painter);
        }

        // Dibujar marcadores de pico si están habilitados
        if (m_showPeakMarkers && m_peakLevel > 0.0) {
            drawPeakMarkers(painter);
        }
    }

    // Dibujar información de estado
    drawStatusInfo(painter);
}

void WaveformWidget::drawGrid(QPainter& painter)
{
    painter.setPen(QPen(m_gridColor, 1));

    const QRect drawRect = rect();
    const int width = drawRect.width();
    const int height = drawRect.height();

    // Líneas verticales (tiempo)
    const int verticalLines = m_gridDivisions;
    for (int i = 1; i < verticalLines; ++i) {
        int x = (width * i) / verticalLines;
        painter.drawLine(x, 0, x, height);
    }

    // Líneas horizontales (amplitud)
    const int horizontalLines = m_gridDivisions;
    for (int i = 1; i < horizontalLines; ++i) {
        int y = (height * i) / horizontalLines;
        painter.drawLine(0, y, width, y);
    }
}

void WaveformWidget::drawZeroLine(QPainter& painter)
{
    painter.setPen(QPen(m_zeroLineColor, 2));

    const int centerY = height() / 2 + static_cast<int>(m_verticalOffset * height() / 2);
    painter.drawLine(0, centerY, width(), centerY);
}

void WaveformWidget::drawWaveform(QPainter& painter)
{
    if (m_waveformData.isEmpty()) return;

    const QRect drawRect = rect();
    const int width = drawRect.width();
    const int height = drawRect.height();
    const int centerY = height / 2;

    // Configurar pen según el modo de renderizado
    QPen waveformPen(m_waveformColor);

    switch (m_renderMode) {
    case RenderMode::Line:
        waveformPen.setWidth(1);
        painter.setPen(waveformPen);
        drawWaveformLine(painter, width, height, centerY);
        break;

    case RenderMode::Filled:
        painter.setPen(QPen(m_waveformColor, 1));
        painter.setBrush(QBrush(QColor(m_waveformColor.red(),
                                       m_waveformColor.green(),
                                       m_waveformColor.blue(),
                                       80)));
        drawWaveformFilled(painter, width, height, centerY);
        break;

    case RenderMode::Bars:
        painter.setPen(QPen(m_waveformColor, 2));
        drawWaveformBars(painter, width, height, centerY);
        break;
    }
}

void WaveformWidget::drawWaveformLine(QPainter& painter, int width, int height, int centerY)
{
    const int dataSize = m_waveformData.size();
    if (dataSize < 2) return;

    QVector<QPointF> points;
    points.reserve(dataSize);

    for (int i = 0; i < dataSize; ++i) {
        double x = (double(i) / (dataSize - 1)) * width;
        double y = centerY - (m_waveformData[i] * m_amplitudeScale * height / 2) +
                   (m_verticalOffset * height / 2);

        // Clamp Y para evitar dibujar fuera del widget
        y = qBound(0.0, y, double(height));

        points.append(QPointF(x, y));
    }

    // Dibujar líneas conectando los puntos
    for (int i = 1; i < points.size(); ++i) {
        painter.drawLine(points[i-1], points[i]);
    }
}

void WaveformWidget::drawWaveformFilled(QPainter& painter, int width, int height, int centerY)
{
    const int dataSize = m_waveformData.size();
    if (dataSize < 2) return;

    QPolygonF polygon;
    polygon.reserve(dataSize + 2);

    // Punto inicial en la línea base
    polygon.append(QPointF(0, centerY + m_verticalOffset * height / 2));

    // Puntos de la forma de onda
    for (int i = 0; i < dataSize; ++i) {
        double x = (double(i) / (dataSize - 1)) * width;
        double y = centerY - (m_waveformData[i] * m_amplitudeScale * height / 2) +
                   (m_verticalOffset * height / 2);
        y = qBound(0.0, y, double(height));
        polygon.append(QPointF(x, y));
    }

    // Punto final en la línea base
    polygon.append(QPointF(width, centerY + m_verticalOffset * height / 2));

    painter.drawPolygon(polygon);
}

void WaveformWidget::drawWaveformBars(QPainter& painter, int width, int height, int centerY)
{
    const int dataSize = m_waveformData.size();
    if (dataSize == 0) return;

    const double barWidth = double(width) / dataSize;
    const double baseY = centerY + m_verticalOffset * height / 2;

    for (int i = 0; i < dataSize; ++i) {
        double x = i * barWidth;
        double amplitude = m_waveformData[i] * m_amplitudeScale * height / 2;
        double y = baseY - amplitude;

        // Clamp para evitar dibujar fuera del widget
        y = qBound(0.0, y, double(height));
        amplitude = qBound(-double(height)/2, amplitude, double(height)/2);

        painter.drawLine(QPointF(x, baseY), QPointF(x, y));
    }
}

void WaveformWidget::drawRMSLine(QPainter& painter)
{
    painter.setPen(QPen(m_rmsLineColor, 2, Qt::DashLine));

    const int height = this->height();
    const int centerY = height / 2;
    const double rmsY = centerY - (m_rmsLevel * m_amplitudeScale * height / 2) +
                        (m_verticalOffset * height / 2);
    const double rmsYNeg = centerY + (m_rmsLevel * m_amplitudeScale * height / 2) +
                           (m_verticalOffset * height / 2);

    // Líneas RMS positiva y negativa
    if (rmsY >= 0 && rmsY <= height) {
        painter.drawLine(0, static_cast<int>(rmsY), width(), static_cast<int>(rmsY));
    }
    if (rmsYNeg >= 0 && rmsYNeg <= height) {
        painter.drawLine(0, static_cast<int>(rmsYNeg), width(), static_cast<int>(rmsYNeg));
    }
}

void WaveformWidget::drawPeakMarkers(QPainter& painter)
{
    if (m_waveformData.isEmpty()) return;

    painter.setPen(QPen(m_peakMarkerColor, 3));

    const int height = this->height();
    const int width = this->width();
    const int centerY = height / 2;
    const int dataSize = m_waveformData.size();

    // Encontrar picos locales
    QVector<int> peakIndices = findPeaks();

    for (int peakIndex : peakIndices) {
        if (peakIndex < 0 || peakIndex >= dataSize) continue;

        double x = (double(peakIndex) / (dataSize - 1)) * width;
        double y = centerY - (m_waveformData[peakIndex] * m_amplitudeScale * height / 2) +
                   (m_verticalOffset * height / 2);

        // Dibujar marcador de pico
        painter.drawEllipse(QPointF(x, y), 4, 4);
    }
}

void WaveformWidget::drawStatusInfo(QPainter& painter)
{
    painter.setPen(m_textColor);

    const int margin = 5;
    const int lineHeight = 15;
    int y = margin + lineHeight;

    // Información básica
    QString info = QString("Duration: %1s | Scale: %2x | Peak: %3 | RMS: %4")
                       .arg(m_displayDuration, 0, 'f', 1)
                       .arg(m_amplitudeScale, 0, 'f', 1)
                       .arg(m_peakLevel, 0, 'f', 3)
                       .arg(m_rmsLevel, 0, 'f', 3);

    painter.drawText(margin, y, info);

    // Información del formato de audio
    if (m_sampleRate > 0) {
        y += lineHeight;
        QString formatInfo = QString("Format: %1 Hz, %2 ch | Samples: %3")
                                 .arg(m_sampleRate)
                                 .arg(m_channels)
                                 .arg(m_waveformData.size());
        painter.drawText(margin, y, formatInfo);
    }
}

void WaveformWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isMouseTracking = true;
        m_lastMousePos = event->pos();
    }
    QWidget::mousePressEvent(event);
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isMouseTracking && (event->buttons() & Qt::LeftButton)) {
        // Implementar pan/drag si es necesario
        QPoint delta = event->pos() - m_lastMousePos;
        m_lastMousePos = event->pos();

        // Ajustar offset vertical basado en el movimiento del mouse
        double offsetDelta = -double(delta.y()) / height();
        setVerticalOffset(m_verticalOffset + offsetDelta);
    }

    // Mostrar información de la posición del cursor
    showCursorInfo(event->pos());

    QWidget::mouseMoveEvent(event);
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        m_isMouseTracking = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void WaveformWidget::wheelEvent(QWheelEvent* event)
{
    // Zoom vertical con la rueda del mouse
    const double zoomFactor = 1.1;
    const double delta = event->angleDelta().y() / 120.0;

    if (delta > 0) {
        setAmplitudeScale(m_amplitudeScale * zoomFactor);
    } else if (delta < 0) {
        setAmplitudeScale(m_amplitudeScale / zoomFactor);
    }

    event->accept();
}

void WaveformWidget::showCursorInfo(const QPoint& pos)
{
    if (m_waveformData.isEmpty()) return;

    const int dataSize = m_waveformData.size();
    const double relativeX = double(pos.x()) / width();
    const int sampleIndex = static_cast<int>(relativeX * (dataSize - 1));

    if (sampleIndex >= 0 && sampleIndex < dataSize) {
        const double timeSeconds = (double(sampleIndex) / dataSize) * m_displayDuration;
        const float amplitude = m_waveformData[sampleIndex];

        QString tooltipText = QString("Time: %1s | Amplitude: %2")
                                  .arg(timeSeconds, 0, 'f', 3)
                                  .arg(amplitude, 0, 'f', 3);

        QToolTip::showText(mapToGlobal(pos), tooltipText, this);
    }
}

void WaveformWidget::calculateStatistics()
{
    if (m_waveformData.isEmpty()) {
        m_peakLevel = m_rmsLevel = 0.0;
        return;
    }

    double sumSquares = 0.0;
    m_peakLevel = 0.0;

    for (float sample : m_waveformData) {
        double absSample = qAbs(sample);
        if (absSample > m_peakLevel) {
            m_peakLevel = absSample;
        }
        sumSquares += sample * sample;
    }

    m_rmsLevel = qSqrt(sumSquares / m_waveformData.size());
}

void WaveformWidget::updateAutoScale()
{
    if (m_peakLevel > 0.0) {
        const double targetScale = 0.8; // Usar 80% de la altura disponible
        m_amplitudeScale = targetScale / m_peakLevel;

        // Limitar la escala automática
        m_amplitudeScale = qBound(0.1, m_amplitudeScale, 5.0);
    }
}

QVector<int> WaveformWidget::findPeaks() const
{
    QVector<int> peaks;

    if (m_waveformData.size() < 3) return peaks;

    const double threshold = m_peakLevel * 0.5; // 50% del pico máximo

    for (int i = 1; i < m_waveformData.size() - 1; ++i) {
        float current = qAbs(m_waveformData[i]);
        float prev = qAbs(m_waveformData[i-1]);
        float next = qAbs(m_waveformData[i+1]);

        if (current > prev && current > next && current > threshold) {
            peaks.append(i);
        }
    }

    return peaks;
}

