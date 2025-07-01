// waveform_widget.cpp
#include "waveform_widget.h"
#include "audio_db.h"
#include <QDebug>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QApplication>
#include <algorithm>
#include <cmath>

WaveformWidget::WaveformWidget(AudioDb* db, QWidget* parent)
    : QWidget(parent)
    , m_db(db)
    , m_updateTimer(new QTimer(this))
{
    if (!m_db) {
        qWarning() << "WaveformWidget: AudioDb es nullptr";
    }

    setMinimumSize(400, 200);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setMouseTracking(true);

    setupTimer();

    // Cargar datos existentes
    if (m_db) {
        loadPeaksFromDatabase();
    }

    m_pollTimer = new QTimer(this);
    connect(m_pollTimer, &QTimer::timeout,
            this, &WaveformWidget::onPollDatabase);
    m_pollTimer->setInterval(m_config.updateIntervalMs);
    m_pollTimer->start();

    m_lastKnownBlock = m_db ? m_db->getTotalBlocks() : 0;

    qDebug() << "WaveformWidget inicializado con" << m_peakData.size() << "bloques";
}

WaveformWidget::~WaveformWidget() {
    if (m_updateTimer) {
        m_updateTimer->stop();
    }
}

void WaveformWidget::setupTimer() {
    connect(m_updateTimer, &QTimer::timeout, this, &WaveformWidget::updateDisplay);
    m_updateTimer->setInterval(m_config.updateIntervalMs);

    if (m_autoUpdate) {
        m_updateTimer->start();
    }
}

void WaveformWidget::setConfig(const WaveformConfig& config) {
    m_config = config;
    m_updateTimer->setInterval(m_config.updateIntervalMs);
    m_needsRecalculation = true;
    update();
}

void WaveformWidget::setAmplitudeScale(float scale) {
    if (scale > 0.0f && scale != m_config.amplitudeScale) {
        m_config.amplitudeScale = scale;
        update();
    }
}

void WaveformWidget::setPixelsPerBlock(int pixels) {
    if (pixels > 0 && pixels != m_config.pixelsPerBlock) {
        m_config.pixelsPerBlock = pixels;
        m_needsRecalculation = true;
        update();
    }
}

void WaveformWidget::setMaxVisibleBlocks(int blocks) {
    if (blocks > 0 && blocks != m_config.maxVisibleBlocks) {
        m_config.maxVisibleBlocks = blocks;
        m_needsRecalculation = true;
        update();
    }
}

void WaveformWidget::clearWaveform() {
    m_peakData.clear();
    m_displayStartBlock = 0;
    m_displayEndBlock = 0;
    m_lastKnownBlocks = 0;
    m_needsRecalculation = true;
    update();
}

void WaveformWidget::refreshFromDatabase() {
    if (m_db) {
        loadPeaksFromDatabase();
        m_needsRecalculation = true;
        update();
    }
}

void WaveformWidget::setAutoUpdate(bool enabled) {
    m_autoUpdate = enabled;

    if (enabled && !m_updateTimer->isActive()) {
        m_updateTimer->start();
    } else if (!enabled && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
}

void WaveformWidget::onNewPeakData(float minValue, float maxValue, qint64 timestamp) {
    Q_UNUSED(timestamp)

    // Agregar nuevo pico
    m_peakData.append(qMakePair(minValue, maxValue));

    // Limitar el número de bloques en memoria
    if (m_peakData.size() > m_config.maxVisibleBlocks * 2) {
        int toRemove = m_peakData.size() - m_config.maxVisibleBlocks;
        m_peakData.erase(m_peakData.begin(), m_peakData.begin() + toRemove);
        m_displayStartBlock = std::max(0, m_displayStartBlock - toRemove);
    }

    // No actualizar inmediatamente para evitar parpadeo
    // El timer se encargará de la actualización
}

void WaveformWidget::updateDisplay() {
    if (!m_db) {
        return;
    }

    // Verificar si hay nuevos datos
    int currentBlocks = m_db->getTotalBlocks();
    if (currentBlocks != m_lastKnownBlocks) {
        // Cargar solo los nuevos bloques
        if (currentBlocks > m_lastKnownBlocks) {
            loadPeaksFromDatabase();
        }
        m_lastKnownBlocks = currentBlocks;
        m_needsRecalculation = true;
    }

    if (m_needsRecalculation) {
        calculateDisplayRange();
        m_needsRecalculation = false;
    }

    update();
}

void WaveformWidget::loadPeaksFromDatabase() {
    if (!m_db) {
        return;
    }

    // Obtener todos los picos de la base de datos
    QList<QPair<float, float>> peaks = m_db->getAllPeaks();

    // Convertir a QVector
    m_peakData.clear();
    m_peakData.reserve(peaks.size());

    for (const auto& peak : peaks) {
        m_peakData.append(peak);
    }

    qDebug() << "WaveformWidget: cargados" << m_peakData.size() << "picos desde la BD";
}

void WaveformWidget::calculateDisplayRange() {
    int totalBlocks = m_peakData.size();
    if (totalBlocks == 0) {
        m_displayStartBlock = 0;
        m_displayEndBlock = 0;
        m_visibleBlocks = 0;
        return;
    }

    // Calcular cuántos bloques caben en el ancho actual
    int widgetWidth = width();
    m_visibleBlocks = std::max(1, widgetWidth / m_config.pixelsPerBlock);

    // Por defecto, mostrar los últimos bloques (modo "live")
    m_displayEndBlock = totalBlocks;
    m_displayStartBlock = std::max(0, m_displayEndBlock - m_visibleBlocks);

    // Aplicar offset horizontal si el usuario ha hecho scroll
    if (m_horizontalOffset != 0.0f) {
        int offsetBlocks = static_cast<int>(m_horizontalOffset / m_config.pixelsPerBlock);
        m_displayStartBlock = std::max(0, std::min(totalBlocks - m_visibleBlocks, offsetBlocks));
        m_displayEndBlock = std::min(totalBlocks, m_displayStartBlock + m_visibleBlocks);
    }
}

void WaveformWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)

    QPainter painter(this);

    if (m_config.antiAliasing) {
        painter.setRenderHint(QPainter::Antialiasing, true);
    }

    // Limpiar fondo
    painter.fillRect(rect(), m_config.backgroundColor);

    if (m_peakData.isEmpty()) {
        // Mostrar mensaje cuando no hay datos
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, "No hay datos de audio");
        return;
    }

    // Dibujar componentes
    if (m_config.showGrid) {
        drawGrid(painter);
    }

    if (m_config.showCenterLine) {
        drawCenterLine(painter);
    }

    drawWaveform(painter);
}

void WaveformWidget::drawWaveform(QPainter& painter) {
    if (m_peakData.isEmpty() || m_displayStartBlock >= m_displayEndBlock) {
        return;
    }

    painter.setPen(QPen(m_config.waveformColor, 1));
    painter.setBrush(QBrush(m_config.waveformColor));

    int centerY = height() / 2;
    float heightScale = (height() / 2) * m_config.amplitudeScale;

    // Dibujar líneas verticales para cada bloque
    for (int blockIdx = m_displayStartBlock; blockIdx < m_displayEndBlock && blockIdx < m_peakData.size(); ++blockIdx) {
        const auto& peak = m_peakData[blockIdx];
        float minVal = peak.first;
        float maxVal = peak.second;

        int x = blockToX(blockIdx - m_displayStartBlock);

        // Convertir valores de muestra a coordenadas Y
        int minY = centerY - static_cast<int>(minVal * heightScale);
        int maxY = centerY - static_cast<int>(maxVal * heightScale);

        // Asegurar que minY > maxY (coordenadas de pantalla)
        if (minY < maxY) {
            std::swap(minY, maxY);
        }

        // Dibujar línea vertical del pico
        if (m_config.pixelsPerBlock > 1) {
            // Si hay espacio, dibujar rectángulo
            QRect blockRect(x, maxY, m_config.pixelsPerBlock, minY - maxY + 1);
            painter.fillRect(blockRect, m_config.waveformColor);
        } else {
            // Si no hay espacio, dibujar línea simple
            painter.drawLine(x, maxY, x, minY);
        }
    }
}

void WaveformWidget::drawGrid(QPainter& painter) {
    painter.setPen(QPen(m_config.gridColor, 1));

    int w = width();
    int h = height();

    // Líneas horizontales (niveles de amplitud)
    int numHLines = 5;
    for (int i = 1; i < numHLines; ++i) {
        int y = (h * i) / numHLines;
        painter.drawLine(0, y, w, y);
    }

    // Líneas verticales (marcas de tiempo)
    int numVLines = 10;
    for (int i = 1; i < numVLines; ++i) {
        int x = (w * i) / numVLines;
        painter.drawLine(x, 0, x, h);
    }
}

void WaveformWidget::drawCenterLine(QPainter& painter) {
    painter.setPen(QPen(m_config.centerLineColor, 1));
    int centerY = height() / 2;
    painter.drawLine(0, centerY, width(), centerY);
}

float WaveformWidget::sampleToY(float sample) const {
    int centerY = height() / 2;
    float heightScale = (height() / 2) * m_config.amplitudeScale;
    return centerY - (sample * heightScale);
}

int WaveformWidget::blockToX(int blockIndex) const {
    return blockIndex * m_config.pixelsPerBlock;
}

void WaveformWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    m_needsRecalculation = true;
}

void WaveformWidget::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom vertical con Ctrl + rueda
        float scaleFactor = 1.0f + (event->angleDelta().y() / 1200.0f);
        setAmplitudeScale(m_config.amplitudeScale * scaleFactor);
    } else if (event->modifiers() & Qt::ShiftModifier) {
        // Zoom horizontal con Shift + rueda
        int newPixelsPerBlock = m_config.pixelsPerBlock + (event->angleDelta().y() / 120);
        newPixelsPerBlock = std::max(1, std::min(20, newPixelsPerBlock));
        setPixelsPerBlock(newPixelsPerBlock);
    } else {
        // Scroll horizontal normal
        float scrollAmount = event->angleDelta().y() / 120.0f * 50.0f;
        m_horizontalOffset = std::max(0.0f, m_horizontalOffset - scrollAmount);
        m_needsRecalculation = true;
        update();
    }

    event->accept();
}

void WaveformWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_horizontalOffset = std::max(0.0f, m_horizontalOffset - delta.x());
        m_lastMousePos = event->pos();
        m_needsRecalculation = true;
        update();
    }
}

QString WaveformWidget::getStatusInfo() const {
    return QString("Waveform: %1 bloques, mostrando %2-%3, escala: %.2fx")
    .arg(m_peakData.size())
        .arg(m_displayStartBlock)
        .arg(m_displayEndBlock)
        .arg(m_config.amplitudeScale);
}


void WaveformWidget::onPollDatabase() {
    if (!m_db) return;

    int total = m_db->getTotalBlocks();
    if (total == m_lastKnownBlock) {
        // Nada nuevo
        return;
    }

    // Traer TODOS los picos (podrías optimizar trayendo sólo un rango)
    QList<QPair<float,float>> peaks = m_db->getAllPeaks();
    m_peakData.clear();
    m_peakData.reserve(peaks.size());
    for (auto &p : peaks)
        m_peakData.append(p);

    // Actualizar contador y repintar
    m_lastKnownBlock = total;
    m_needsRecalculation = true;
    update();
}

