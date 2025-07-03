#include "waveform_widget.h"
#include <QPainter>
#include <QPaintEvent>
#include <QtMath>
#include <QPainterPath>
#include <QMutexLocker>
#include <algorithm>

WaveformWidget::WaveformWidget(QWidget* parent)
    : QWidget(parent)
{
    recalcBuffer();
}

void WaveformWidget::recalcBuffer() {
    QMutexLocker locker(&m_bufferMutex);
    m_totalSamples = qMax(1, int(m_sampleRate * m_timeWindowSeconds));
    m_buffer.fill(0.0f, m_totalSamples);
    m_writeIndex = 0;
    update();
}

void WaveformWidget::appendFrames(const QVector<FrameData>& frames) {
    {
        QMutexLocker locker(&m_bufferMutex);

        if (m_totalSamples <= 0 || m_buffer.size() != m_totalSamples) {
            return;
        }

        for (const auto& frame : frames) {
            const QVector<float>& waveform = frame.waveform;
            int samplesToWrite = qMin(waveform.size(), m_totalSamples);

            for (int i = 0; i < samplesToWrite; ++i) {
                // 15. VALIDACIÓN de entrada más estricta
                float value = waveform[i];
                if (qIsFinite(value)) {
                    m_buffer[m_writeIndex] = qBound(-1.0f, value, 1.0f);
                } else {
                    m_buffer[m_writeIndex] = 0.0f; // Valor seguro
                }
                m_writeIndex = (m_writeIndex + 1) % m_totalSamples;
            }
        }
    }

    // 16. ACTUALIZACIÓN controlada
    update(); // Usar update() en lugar de repaint() para mejor rendimiento
}

void WaveformWidget::setWaveformSize(int size) {
    QMutexLocker locker(&m_bufferMutex);
    m_totalSamples = qMax(1, size);
    m_buffer.fill(0.0f, m_totalSamples);
    m_writeIndex = 0;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 1. LIMPIEZA COMPLETA - Asegurar que todo el área se limpia
    painter.fillRect(rect(), m_bgColor);

    // 2. También limpiar el área del evento específicamente
    if (event) {
        painter.fillRect(event->rect(), m_bgColor);
    }

    const int widgetWidth = width();
    const int widgetHeight = height();
    const int midY = widgetHeight / 2;

    // Dibujar rejilla
    if (m_showGrid) {
        drawGrid(painter, widgetWidth, widgetHeight, midY);
    }

    // Línea central
    painter.setPen(QPen(m_gridColor.darker(150), 1));
    painter.drawLine(0, midY, widgetWidth, midY);

    // 3. COPIA THREAD-SAFE mejorada
    QVector<float> bufferCopy;
    int writeIndexCopy;
    int totalSamplesCopy;

    {
        QMutexLocker locker(&m_bufferMutex);
        if (m_totalSamples <= 0 || m_buffer.isEmpty()) {
            return;
        }
        bufferCopy = m_buffer;
        writeIndexCopy = m_writeIndex;
        totalSamplesCopy = m_totalSamples;
    }

    // 4. VALIDACIÓN adicional antes de dibujar
    if (bufferCopy.size() != totalSamplesCopy) {
        return; // Evitar inconsistencias
    }

    // Dibujar waveform
    drawWaveform(painter, bufferCopy, writeIndexCopy, totalSamplesCopy,
                 widgetWidth, widgetHeight, midY);
}

void WaveformWidget::drawGrid(QPainter& painter, int width, int height, int midY) {
    painter.setPen(m_gridColor);

    // Líneas horizontales
    const int gridLines = 4;
    for (int i = 1; i < gridLines; ++i) {
        int y = midY + (i - gridLines/2) * (height / (gridLines + 1));
        painter.drawLine(0, y, width, y);
    }

    // Líneas verticales
    const int verticalLines = 10;
    for (int i = 1; i < verticalLines; ++i) {
        int x = i * width / verticalLines;
        painter.drawLine(x, 0, x, height);
    }
}

void WaveformWidget::drawWaveform(QPainter& painter, const QVector<float>& buffer,
                                  int writeIndex, int totalSamples,
                                  int width, int height, int midY) {
    if (width <= 0 || totalSamples <= 0) return;

    painter.setPen(QPen(m_waveColor, 1.2));

    // Calcular cuántas muestras por pixel
    const float samplesPerPixel = float(totalSamples) / float(width);
    const int maxAmplitude = height / 2 - 2;

    // 5. CREAR PATH LIMPIO en cada frame
    QPainterPath path;

    // 6. COMENZAR desde el primer punto real, no desde (0, midY)
    bool pathStarted = false;

    if (samplesPerPixel >= 1.0f) {
        // Muchas muestras por pixel: usar min/max
        drawWaveformDownsampled(path, buffer, writeIndex, totalSamples,
                                width, midY, maxAmplitude, samplesPerPixel, pathStarted);
    } else {
        // Pocas muestras por pixel: interpolación
        drawWaveformUpsampled(path, buffer, writeIndex, totalSamples,
                              width, midY, maxAmplitude, samplesPerPixel, pathStarted);
    }

    // 7. SOLO dibujar si hay datos válidos
    if (pathStarted && !path.isEmpty()) {
        painter.drawPath(path);
    }
}

void WaveformWidget::drawWaveformDownsampled(QPainterPath& path,
                                             const QVector<float>& buffer,
                                             int writeIndex, int totalSamples,
                                             int width, int midY, int maxAmplitude,
                                             float samplesPerPixel, bool& pathStarted) {
    for (int x = 0; x < width; ++x) {
        int startSample = int(x * samplesPerPixel);
        int endSample = int((x + 1) * samplesPerPixel);
        int sampleCount = endSample - startSample;

        float minValue = 1.0f;
        float maxValue = -1.0f;
        bool hasData = false;

        // Encontrar min/max en el rango
        for (int i = 0; i < sampleCount; ++i) {
            int bufferIndex = (writeIndex + startSample + i) % totalSamples;

            // 8. VALIDACIÓN más estricta de índices
            if (bufferIndex >= 0 && bufferIndex < buffer.size()) {
                float value = buffer[bufferIndex];

                // 9. VALIDAR que el valor sea válido
                if (qIsFinite(value) && value >= -1.0f && value <= 1.0f) {
                    minValue = qMin(minValue, value);
                    maxValue = qMax(maxValue, value);
                    hasData = true;
                }
            }
        }

        if (hasData) {
            // Convertir a coordenadas de pantalla
            int y1 = midY - qBound(-maxAmplitude, int(maxValue * maxAmplitude), maxAmplitude);
            int y2 = midY - qBound(-maxAmplitude, int(minValue * maxAmplitude), maxAmplitude);

            // 10. INICIAR path correctamente
            if (!pathStarted) {
                path.moveTo(x, y1);
                pathStarted = true;
            } else {
                path.lineTo(x, y1);
            }

            if (y1 != y2) {
                path.lineTo(x, y2);
            }
        } else if (pathStarted) {
            // Si no hay datos, conectar a la línea central
            path.lineTo(x, midY);
        }
    }
}


void WaveformWidget::drawWaveformUpsampled(QPainterPath& path,
                                           const QVector<float>& buffer,
                                           int writeIndex, int totalSamples,
                                           int width, int midY, int maxAmplitude,
                                           float samplesPerPixel, bool& pathStarted) {
    for (int x = 0; x < width; ++x) {
        float samplePos = x * samplesPerPixel;
        int sampleIndex = int(samplePos);
        float fraction = samplePos - sampleIndex;

        int bufferIndex1 = (writeIndex + sampleIndex) % totalSamples;
        int bufferIndex2 = (writeIndex + sampleIndex + 1) % totalSamples;

        // 11. VALIDACIÓN mejorada para interpolación
        if (bufferIndex1 >= 0 && bufferIndex1 < buffer.size() &&
            bufferIndex2 >= 0 && bufferIndex2 < buffer.size()) {

            float value1 = buffer[bufferIndex1];
            float value2 = buffer[bufferIndex2];

            // 12. VALIDAR valores antes de interpolar
            if (qIsFinite(value1) && qIsFinite(value2) &&
                value1 >= -1.0f && value1 <= 1.0f &&
                value2 >= -1.0f && value2 <= 1.0f) {

                float interpolatedValue = value1 + fraction * (value2 - value1);
                int y = midY - qBound(-maxAmplitude, int(interpolatedValue * maxAmplitude), maxAmplitude);

                if (!pathStarted) {
                    path.moveTo(x, y);
                    pathStarted = true;
                } else {
                    path.lineTo(x, y);
                }
            }
        } else if (pathStarted) {
            path.lineTo(x, midY);
        }
    }
}


void WaveformWidget::clearWaveform() {
    QMutexLocker locker(&m_bufferMutex);
    m_buffer.fill(0.0f);
    m_writeIndex = 0;

    // Forzar repintado completo
    update();
    repaint(); // Repintado inmediato
}


