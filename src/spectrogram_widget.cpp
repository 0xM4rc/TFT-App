#include "include/spectrogram_widget.h"
#include <QPainter>
#include <QFontMetrics>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QtMath>
#include <QDebug>

// Constantes por defecto
const float SpectrogramWidget::DEFAULT_MIN_FREQUENCY = 20.0f;
const float SpectrogramWidget::DEFAULT_MAX_FREQUENCY = 20000.0f;
const float SpectrogramWidget::DEFAULT_MIN_AMPLITUDE = -80.0f;
const float SpectrogramWidget::DEFAULT_MAX_AMPLITUDE = 0.0f;

SpectrogramWidget::SpectrogramWidget(QWidget *parent)
    : QWidget(parent)
    , m_colorMap(Jet)
    , m_displayMode(Waterfall)
    , m_maxHistory(DEFAULT_MAX_HISTORY)
    , m_minFrequency(DEFAULT_MIN_FREQUENCY)
    , m_maxFrequency(DEFAULT_MAX_FREQUENCY)
    , m_sampleRate(44100)
    , m_minAmplitude(DEFAULT_MIN_AMPLITUDE)
    , m_maxAmplitude(DEFAULT_MAX_AMPLITUDE)
    , m_interpolationEnabled(true)
    , m_showGrid(true)
    , m_showFrequencyLabels(true)
    , m_showTimeLabels(true)
    , m_showColorBar(true)
    , m_autoUpdate(true)
    , m_updateRate(DEFAULT_UPDATE_RATE)
    , m_imageNeedsUpdate(true)
    , m_updateTimer(new QTimer(this))
    , m_startTime(0.0)
    , m_isSelecting(false)
    , m_contextMenu(nullptr)
{
    setupUI();
    generateColorMap();

    // Configurar timer de actualización
    m_updateTimer->setSingleShot(false);
    connect(m_updateTimer, &QTimer::timeout, this, &SpectrogramWidget::updateDisplay);

    if (m_autoUpdate) {
        m_updateTimer->start(1000 / m_updateRate);
    }

    // Inicializar timer de tiempo transcurrido
    m_elapsedTimer.start();

    qDebug() << "SpectrogramWidget initialized";
}

SpectrogramWidget::~SpectrogramWidget()
{
    qDebug() << "SpectrogramWidget destroyed";
}

void SpectrogramWidget::setupUI()
{
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(true);

    // Configurar widget
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);

    createContextMenu();
}

void SpectrogramWidget::createContextMenu()
{
    m_contextMenu = new QMenu(this);

    // Acciones de archivo
    m_actionSaveImage = m_contextMenu->addAction("Save Image...");
    connect(m_actionSaveImage, &QAction::triggered, this, [this]() {
        QString filename = QFileDialog::getSaveFileName(this, "Save Spectrogram",
                                                        "", "PNG Files (*.png);;JPEG Files (*.jpg)");
        if (!filename.isEmpty()) {
            saveImage(filename);
        }
    });

    m_actionCopyClipboard = m_contextMenu->addAction("Copy to Clipboard");
    connect(m_actionCopyClipboard, &QAction::triggered, this, &SpectrogramWidget::copyToClipboard);

    m_contextMenu->addSeparator();

    // Acciones de datos
    m_actionClearHistory = m_contextMenu->addAction("Clear History");
    connect(m_actionClearHistory, &QAction::triggered, this, &SpectrogramWidget::clearHistory);

    m_contextMenu->addSeparator();

    // Submenu de mapas de color
    QMenu* colorMapMenu = m_contextMenu->addMenu("Color Map");

    m_actionColorMapJet = colorMapMenu->addAction("Jet");
    m_actionColorMapJet->setCheckable(true);
    connect(m_actionColorMapJet, &QAction::triggered, this, [this]() { setColorMap(Jet); });

    m_actionColorMapHot = colorMapMenu->addAction("Hot");
    m_actionColorMapHot->setCheckable(true);
    connect(m_actionColorMapHot, &QAction::triggered, this, [this]() { setColorMap(Hot); });

    m_actionColorMapCool = colorMapMenu->addAction("Cool");
    m_actionColorMapCool->setCheckable(true);
    connect(m_actionColorMapCool, &QAction::triggered, this, [this]() { setColorMap(Cool); });

    m_actionColorMapGrayscale = colorMapMenu->addAction("Grayscale");
    m_actionColorMapGrayscale->setCheckable(true);
    connect(m_actionColorMapGrayscale, &QAction::triggered, this, [this]() { setColorMap(Grayscale); });

    m_actionColorMapViridis = colorMapMenu->addAction("Viridis");
    m_actionColorMapViridis->setCheckable(true);
    connect(m_actionColorMapViridis, &QAction::triggered, this, [this]() { setColorMap(Viridis); });

    m_contextMenu->addSeparator();

    // Acciones de visualización
    m_actionToggleGrid = m_contextMenu->addAction("Show Grid");
    m_actionToggleGrid->setCheckable(true);
    m_actionToggleGrid->setChecked(m_showGrid);
    connect(m_actionToggleGrid, &QAction::toggled, this, &SpectrogramWidget::setShowGrid);

    m_actionToggleLabels = m_contextMenu->addAction("Show Labels");
    m_actionToggleLabels->setCheckable(true);
    m_actionToggleLabels->setChecked(m_showFrequencyLabels);
    connect(m_actionToggleLabels, &QAction::toggled, this, &SpectrogramWidget::setShowFrequencyLabels);

    m_actionToggleColorBar = m_contextMenu->addAction("Show Color Bar");
    m_actionToggleColorBar->setCheckable(true);
    m_actionToggleColorBar->setChecked(m_showColorBar);
    connect(m_actionToggleColorBar, &QAction::toggled, this, &SpectrogramWidget::setShowColorBar);
}

void SpectrogramWidget::setColorMap(ColorMap colorMap)
{
    if (m_colorMap == colorMap) return;

    m_colorMap = colorMap;
    generateColorMap();
    m_imageNeedsUpdate = true;

    // Actualizar checkboxes del menú
    if (m_contextMenu) {
        m_actionColorMapJet->setChecked(colorMap == Jet);
        m_actionColorMapHot->setChecked(colorMap == Hot);
        m_actionColorMapCool->setChecked(colorMap == Cool);
        m_actionColorMapGrayscale->setChecked(colorMap == Grayscale);
        m_actionColorMapViridis->setChecked(colorMap == Viridis);
    }

    update();
    qDebug() << "Color map changed to:" << colorMap;
}

void SpectrogramWidget::setDisplayMode(DisplayMode mode)
{
    if (m_displayMode == mode) return;

    m_displayMode = mode;
    m_imageNeedsUpdate = true;
    update();
    qDebug() << "Display mode changed to:" << mode;
}

void SpectrogramWidget::setMaxHistory(int frames)
{
    if (frames <= 0 || frames > 2000) {
        qWarning() << "Invalid max history:" << frames;
        return;
    }

    QMutexLocker locker(&m_dataMutex);
    m_maxHistory = frames;
    trimHistory();
    m_imageNeedsUpdate = true;
    update();
    qDebug() << "Max history set to:" << frames;
}

void SpectrogramWidget::setFrequencyRange(float minFreq, float maxFreq)
{
    if (minFreq >= maxFreq || minFreq < 0 || maxFreq <= 0) {
        qWarning() << "Invalid frequency range:" << minFreq << "-" << maxFreq;
        return;
    }

    m_minFrequency = minFreq;
    m_maxFrequency = maxFreq;
    m_imageNeedsUpdate = true;
    update();
    qDebug() << "Frequency range set to:" << minFreq << "-" << maxFreq << "Hz";
}

void SpectrogramWidget::getFrequencyRange(float& minFreq, float& maxFreq) const
{
    minFreq = m_minFrequency;
    maxFreq = m_maxFrequency;
}

void SpectrogramWidget::setSampleRate(int sampleRate)
{
    if (sampleRate <= 0) {
        qWarning() << "Invalid sample rate:" << sampleRate;
        return;
    }

    m_sampleRate = sampleRate;

    // Ajustar rango de frecuencia si es necesario
    float nyquist = sampleRate / 2.0f;
    if (m_maxFrequency > nyquist) {
        m_maxFrequency = nyquist;
        m_imageNeedsUpdate = true;
        update();
    }

    qDebug() << "Sample rate set to:" << sampleRate << "Hz";
}

void SpectrogramWidget::setAmplitudeRange(float minDb, float maxDb)
{
    if (minDb >= maxDb) {
        qWarning() << "Invalid amplitude range:" << minDb << "-" << maxDb;
        return;
    }

    m_minAmplitude = minDb;
    m_maxAmplitude = maxDb;
    m_imageNeedsUpdate = true;
    update();
    qDebug() << "Amplitude range set to:" << minDb << "-" << maxDb << "dB";
}

void SpectrogramWidget::getAmplitudeRange(float& minDb, float& maxDb) const
{
    minDb = m_minAmplitude;
    maxDb = m_maxAmplitude;
}

void SpectrogramWidget::setInterpolation(bool enabled)
{
    m_interpolationEnabled = enabled;
    update();
}

void SpectrogramWidget::setShowGrid(bool show)
{
    m_showGrid = show;
    if (m_actionToggleGrid) {
        m_actionToggleGrid->setChecked(show);
    }
    update();
}

void SpectrogramWidget::setShowFrequencyLabels(bool show)
{
    m_showFrequencyLabels = show;
    if (m_actionToggleLabels) {
        m_actionToggleLabels->setChecked(show);
    }
    update();
}

void SpectrogramWidget::setShowTimeLabels(bool show)
{
    m_showTimeLabels = show;
    update();
}

void SpectrogramWidget::setShowColorBar(bool show)
{
    m_showColorBar = show;
    if (m_actionToggleColorBar) {
        m_actionToggleColorBar->setChecked(show);
    }
    updateGeometry();
    update();
}

void SpectrogramWidget::setAutoUpdate(bool enabled)
{
    m_autoUpdate = enabled;

    if (enabled && !m_updateTimer->isActive()) {
        m_updateTimer->start(1000 / m_updateRate);
    } else if (!enabled && m_updateTimer->isActive()) {
        m_updateTimer->stop();
    }
}

void SpectrogramWidget::setUpdateRate(int fps)
{
    if (fps < 1 || fps > 120) {
        qWarning() << "Invalid update rate:" << fps;
        return;
    }

    m_updateRate = fps;

    if (m_updateTimer->isActive()) {
        m_updateTimer->setInterval(1000 / fps);
    }
}

void SpectrogramWidget::updateSpectrogram(const QVector<float>& spectrum)
{
    if (spectrum.isEmpty()) return;

    addSpectrumFrame(spectrum);

    if (!m_autoUpdate) {
        update();
    }
}

void SpectrogramWidget::updateSpectrogram(const QVector<QVector<float>>& spectrogramData)
{
    QMutexLocker locker(&m_dataMutex);

    m_spectrogramData = spectrogramData;
    trimHistory();
    m_imageNeedsUpdate = true;

    if (!m_autoUpdate) {
        update();
    }
}

void SpectrogramWidget::addSpectrumFrame(const QVector<float>& spectrum)
{
    QMutexLocker locker(&m_dataMutex);

    m_spectrogramData.append(spectrum);
    trimHistory();
    m_imageNeedsUpdate = true;
}

void SpectrogramWidget::trimHistory()
{
    // Ya debe estar dentro de un mutex lock
    while (m_spectrogramData.size() > m_maxHistory) {
        m_spectrogramData.removeFirst();
    }
}

void SpectrogramWidget::clearHistory()
{
    QMutexLocker locker(&m_dataMutex);
    m_spectrogramData.clear();
    m_imageNeedsUpdate = true;
    m_startTime = m_elapsedTimer.elapsed() / 1000.0;
    update();
    qDebug() << "Spectrogram history cleared";
}

void SpectrogramWidget::generateColorMap()
{
    const int colorMapSize = 256;
    m_colorMapData.clear();
    m_colorMapData.reserve(colorMapSize);

    for (int i = 0; i < colorMapSize; ++i) {
        float t = (float)i / (colorMapSize - 1);
        QColor color;

        switch (m_colorMap) {
        case Jet:
            if (t < 0.25f) {
                color = QColor(0, 0, (int)(255 * (0.5f + 2.0f * t)));
            } else if (t < 0.5f) {
                color = QColor(0, (int)(255 * (4.0f * t - 1.0f)), 255);
            } else if (t < 0.75f) {
                color = QColor((int)(255 * (4.0f * t - 2.0f)), 255, (int)(255 * (3.0f - 4.0f * t)));
            } else {
                color = QColor(255, (int)(255 * (4.0f - 4.0f * t)), 0);
            }
            break;

        case Hot:
            if (t < 0.33f) {
                color = QColor((int)(255 * 3.0f * t), 0, 0);
            } else if (t < 0.66f) {
                color = QColor(255, (int)(255 * (3.0f * t - 1.0f)), 0);
            } else {
                color = QColor(255, 255, (int)(255 * (3.0f * t - 2.0f)));
            }
            break;

        case Cool:
            color = QColor((int)(255 * t), (int)(255 * (1.0f - t)), 255);
            break;

        case Grayscale:
            color = QColor((int)(255 * t), (int)(255 * t), (int)(255 * t));
            break;

        case Viridis:
            // Aproximación del colormap Viridis
            if (t < 0.25f) {
                float s = t * 4.0f;
                color = QColor((int)(68 + s * (59 - 68)), (int)(1 + s * (82 - 1)), (int)(84 + s * (140 - 84)));
            } else if (t < 0.5f) {
                float s = (t - 0.25f) * 4.0f;
                color = QColor((int)(59 + s * (33 - 59)), (int)(82 + s * (144 - 82)), (int)(140 + s * (141 - 140)));
            } else if (t < 0.75f) {
                float s = (t - 0.5f) * 4.0f;
                color = QColor((int)(33 + s * (94 - 33)), (int)(144 + s * (201 - 144)), (int)(141 + s * (129 - 141)));
            } else {
                float s = (t - 0.75f) * 4.0f;
                color = QColor((int)(94 + s * (253 - 94)), (int)(201 + s * (231 - 201)), (int)(129 + s * (37 - 129)));
            }
            break;

        case Plasma:
            // Aproximación del colormap Plasma
            if (t < 0.33f) {
                float s = t * 3.0f;
                color = QColor((int)(13 + s * (126 - 13)), (int)(8 + s * (3 - 8)), (int)(135 + s * (167 - 135)));
            } else if (t < 0.66f) {
                float s = (t - 0.33f) * 3.0f;
                color = QColor((int)(126 + s * (224 - 126)), (int)(3 + s * (78 - 3)), (int)(167 + s * (46 - 167)));
            } else {
                float s = (t - 0.66f) * 3.0f;
                color = QColor((int)(224 + s * (240 - 224)), (int)(78 + s * (249 - 78)), (int)(46 + s * (33 - 46)));
            }
            break;

        case Inferno:
            // Aproximación del colormap Inferno
            if (t < 0.25f) {
                float s = t * 4.0f;
                color = QColor((int)(0 + s * (87 - 0)), (int)(0 + s * (16 - 0)), (int)(4 + s * (109 - 4)));
            } else if (t < 0.5f) {
                float s = (t - 0.25f) * 4.0f;
                color = QColor((int)(87 + s * (187 - 87)), (int)(16 + s * (55 - 16)), (int)(109 + s * (84 - 109)));
            } else if (t < 0.75f) {
                float s = (t - 0.5f) * 4.0f;
                color = QColor((int)(187 + s * (249 - 187)), (int)(55 + s * (142 - 55)), (int)(84 + s * (8 - 84)));
            } else {
                float s = (t - 0.75f) * 4.0f;
                color = QColor((int)(249 + s * (252 - 249)), (int)(142 + s * (255 - 142)), (int)(8 + s * (164 - 8)));
            }
            break;
        }

        m_colorMapData.append(color);
    }

    m_imageNeedsUpdate = true;
}

void SpectrogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Fondo
    painter.fillRect(rect(), Qt::black);

    // Renderizar espectrograma
    renderSpectrogram(painter);

    // Renderizar elementos de UI
    if (m_showGrid) {
        renderGrid(painter);
    }

    if (m_showFrequencyLabels) {
        renderFrequencyLabels(painter);
    }

    if (m_showTimeLabels) {
        renderTimeLabels(painter);
    }

    if (m_showColorBar) {
        renderColorBar(painter);
    }

    // Renderizar selección
    if (m_isSelecting) {
        renderSelection(painter);
    }
}

void SpectrogramWidget::renderSpectrogram(QPainter& painter)
{
    if (m_imageNeedsUpdate) {
        updateSpectrogramImage();
    }

    if (m_spectrogramImage.isNull()) {
        return;
    }

    // Calcular rectángulo de renderizado
    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    // Renderizar imagen del espectrograma
    painter.setRenderHint(QPainter::SmoothPixmapTransform, m_interpolationEnabled);
    painter.drawImage(spectrogramRect, m_spectrogramImage);
}

void SpectrogramWidget::updateSpectrogramImage()
{
    QMutexLocker locker(&m_dataMutex);

    if (m_spectrogramData.isEmpty()) {
        m_spectrogramImage = QImage();
        m_imageNeedsUpdate = false;
        return;
    }

    int timeFrames = m_spectrogramData.size();
    int freqBins = m_spectrogramData.first().size();

    if (timeFrames == 0 || freqBins == 0) {
        m_spectrogramImage = QImage();
        m_imageNeedsUpdate = false;
        return;
    }

    // Crear imagen
    m_spectrogramImage = QImage(timeFrames, freqBins, QImage::Format_RGB32);

    // Llenar imagen
    for (int t = 0; t < timeFrames; ++t) {
        const QVector<float>& spectrum = m_spectrogramData[t];

        for (int f = 0; f < freqBins; ++f) {
            float amplitude = (f < spectrum.size()) ? spectrum[f] : m_minAmplitude;
            QColor color = amplitudeToColor(amplitude);

            int y = freqBins - 1 - f;
            m_spectrogramImage.setPixel(t, y, color.rgb());
        }
    }

    m_imageNeedsUpdate = false;
}

QColor SpectrogramWidget::amplitudeToColor(float amplitudeDb) const
{
    // Normalizar amplitud al rango [0, 1]
    float normalized = (amplitudeDb - m_minAmplitude) / (m_maxAmplitude - m_minAmplitude);
    normalized = qBound(0.0f, normalized, 1.0f);

    // Mapear a índice del color map
    int index = (int)(normalized * (m_colorMapData.size() - 1));
    index = qBound(0, index, m_colorMapData.size() - 1);

    return m_colorMapData[index];
}

void SpectrogramWidget::renderGrid(QPainter& painter)
{
    painter.setPen(QPen(QColor(80, 80, 80), 1, Qt::DotLine));

    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    // Líneas de frecuencia horizontales
    QVector<float> freqLines = {100, 200, 500, 1000, 2000, 5000, 10000, 15000};
    for (float freq : freqLines) {
        if (freq >= m_minFrequency && freq <= m_maxFrequency) {
            float y = frequencyToY(freq);
            if (y >= spectrogramRect.top() && y <= spectrogramRect.bottom()) {
                painter.drawLine(spectrogramRect.left(), y, spectrogramRect.right(), y);
            }
        }
    }

    // Líneas de tiempo verticales (cada 5 segundos)
    double currentTime = m_elapsedTimer.elapsed() / 1000.0;
    double timeWindow = 30.0; // 30 segundos de ventana visible

    for (int i = 0; i <= 6; ++i) {
        double time = currentTime - timeWindow + (i * 5.0);
        double x = timeToX(time);
        if (x >= spectrogramRect.left() && x <= spectrogramRect.right()) {
            painter.drawLine(x, spectrogramRect.top(), x, spectrogramRect.bottom());
        }
    }
}

void SpectrogramWidget::renderFrequencyLabels(QPainter& painter)
{
    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Arial", 8));

    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    // Etiquetas de frecuencia
    QVector<float> freqLabels = {100, 200, 500, 1000, 2000, 5000, 10000, 15000};
    for (float freq : freqLabels) {
        if (freq >= m_minFrequency && freq <= m_maxFrequency) {
            float y = frequencyToY(freq);
            if (y >= spectrogramRect.top() && y <= spectrogramRect.bottom()) {
                QString label;
                if (freq >= 1000) {
                    label = QString("%1k").arg(freq / 1000.0, 0, 'f', freq >= 10000 ? 0 : 1);
                } else {
                    label = QString("%1").arg(freq, 0, 'f', 0);
                }

                QRect textRect(5, y - 8, LEFT_MARGIN - 10, 16);
                painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
            }
        }
    }

    // Etiqueta del eje Y
    painter.save();
    painter.translate(15, spectrogramRect.center().y());
    painter.rotate(-90);
    painter.drawText(-50, 0, 100, 20, Qt::AlignCenter, "Frequency (Hz)");
    painter.restore();
}

void SpectrogramWidget::renderTimeLabels(QPainter& painter)
{
    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Arial", 8));

    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    // Etiquetas de tiempo
    double currentTime = m_elapsedTimer.elapsed() / 1000.0;
    double timeWindow = 30.0; // 30 segundos de ventana visible

    for (int i = 0; i <= 6; ++i) {
        double time = currentTime - timeWindow + (i * 5.0);
        double x = timeToX(time);
        if (x >= spectrogramRect.left() && x <= spectrogramRect.right()) {
            QString label = QString("%1s").arg(time, 0, 'f', 0);
            QRect textRect(x - 20, height() - BOTTOM_MARGIN + 5, 40, 15);
            painter.drawText(textRect, Qt::AlignCenter, label);
        }
    }

    // Etiqueta del eje X
    QRect xLabelRect(spectrogramRect.left(), height() - 15, spectrogramRect.width(), 15);
    painter.drawText(xLabelRect, Qt::AlignCenter, "Time");
}

void SpectrogramWidget::renderColorBar(QPainter& painter)
{
    if (!m_showColorBar) return;

    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - RIGHT_MARGIN,
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    int colorBarLeft = spectrogramRect.right() + COLORBAR_MARGIN;
    QRect colorBarRect(colorBarLeft, spectrogramRect.top(), COLORBAR_WIDTH, spectrogramRect.height());

    // Dibujar barra de color
    for (int y = 0; y < colorBarRect.height(); ++y) {
        float t = 1.0f - (float)y / (colorBarRect.height() - 1);
        int colorIndex = (int)(t * (m_colorMapData.size() - 1));
        colorIndex = qBound(0, colorIndex, m_colorMapData.size() - 1);

        QColor color = m_colorMapData[colorIndex];
        painter.fillRect(colorBarLeft, colorBarRect.top() + y, COLORBAR_WIDTH, 1, color);
    }

    // Marco de la barra de color
    painter.setPen(QColor(128, 128, 128));
    painter.drawRect(colorBarRect);

    // Etiquetas de amplitud
    painter.setPen(QColor(200, 200, 200));
    painter.setFont(QFont("Arial", 8));

    QVector<float> ampLabels = {0, -10, -20, -30, -40, -50, -60, -70, -80};
    for (float amp : ampLabels) {
        if (amp >= m_minAmplitude && amp <= m_maxAmplitude) {
            float t = (amp - m_minAmplitude) / (m_maxAmplitude - m_minAmplitude);
            int y = colorBarRect.bottom() - (int)(t * colorBarRect.height());

            QString label = QString("%1").arg(amp, 0, 'f', 0);
            QRect textRect(colorBarLeft + COLORBAR_WIDTH + 5, y - 8, 40, 16);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        }
    }

    // Etiqueta de la barra de color
    painter.save();
    painter.translate(width() - 15, spectrogramRect.center().y());
    painter.rotate(-90);
    painter.drawText(-30, 0, 60, 20, Qt::AlignCenter, "dB");
    painter.restore();
}

void SpectrogramWidget::renderSelection(QPainter& painter)
{
    if (!m_isSelecting) return;

    painter.setPen(QPen(Qt::yellow, 2));
    painter.setBrush(QBrush(QColor(255, 255, 0, 50)));
    painter.drawRect(m_selectionRect);
}

float SpectrogramWidget::frequencyToY(float frequency) const
{
    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    // Escala logarítmica para frecuencias
    float logMin = qLn(m_minFrequency);
    float logMax = qLn(m_maxFrequency);
    float logFreq = qLn(frequency);

    float t = (logFreq - logMin) / (logMax - logMin);
    return spectrogramRect.bottom() - t * spectrogramRect.height();
}

float SpectrogramWidget::yToFrequency(float y) const
{
    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    float t = (spectrogramRect.bottom() - y) / spectrogramRect.height();
    t = qBound(0.0f, t, 1.0f);

    float logMin = qLn(m_minFrequency);
    float logMax = qLn(m_maxFrequency);
    float logFreq = logMin + t * (logMax - logMin);

    return qExp(logFreq);
}

double SpectrogramWidget::timeToX(double timeSeconds) const
{
    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    double currentTime = m_elapsedTimer.elapsed() / 1000.0;
    double timeWindow = 30.0; // 30 segundos de ventana

    double t = (timeSeconds - (currentTime - timeWindow)) / timeWindow;
    return spectrogramRect.left() + t * spectrogramRect.width();
}

double SpectrogramWidget::xToTime(double x) const
{
    QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                          width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                          height() - TOP_MARGIN - BOTTOM_MARGIN);

    double t = (x - spectrogramRect.left()) / spectrogramRect.width();
    t = qBound(0.0, t, 1.0);

    double currentTime = m_elapsedTimer.elapsed() / 1000.0;
    double timeWindow = 30.0;

    return (currentTime - timeWindow) + t * timeWindow;
}

void SpectrogramWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_imageNeedsUpdate = true;
    updateGeometry();
}

void SpectrogramWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isSelecting = true;
        m_selectionStart = event->pos();
        m_selectionEnd = event->pos();
        m_selectionRect = QRect(m_selectionStart, m_selectionEnd).normalized();

        // Emitir señales de clic
        float freq = yToFrequency(event->pos().y());
        double time = xToTime(event->pos().x());

        emit frequencyClicked(freq);
        emit timeClicked(time);
    }
}

void SpectrogramWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isSelecting) {
        m_selectionEnd = event->pos();
        m_selectionRect = QRect(m_selectionStart, m_selectionEnd).normalized();
        update();
    }
}

void SpectrogramWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_isSelecting) {
        m_isSelecting = false;

        // Calcular selección
        QRect spectrogramRect(LEFT_MARGIN, TOP_MARGIN,
                              width() - LEFT_MARGIN - (m_showColorBar ? RIGHT_MARGIN : 20),
                              height() - TOP_MARGIN - BOTTOM_MARGIN);

        if (m_selectionRect.intersects(spectrogramRect)) {
            QRect intersection = m_selectionRect.intersected(spectrogramRect);

            float minFreq = yToFrequency(intersection.bottom());
            float maxFreq = yToFrequency(intersection.top());
            double startTime = xToTime(intersection.left());
            double endTime = xToTime(intersection.right());

            emit selectionChanged(minFreq, maxFreq, startTime, endTime);
        }

        update();
    }
}

void SpectrogramWidget::wheelEvent(QWheelEvent *event)
{
    // Zoom en frecuencia
    float zoomFactor = 1.1f;
    if (event->angleDelta().y() < 0) {
        zoomFactor = 1.0f / zoomFactor;
    }

    float centerFreq = yToFrequency(event->position().y());
    float freqRange = m_maxFrequency - m_minFrequency;
    float newRange = freqRange * zoomFactor;

    float newMinFreq = centerFreq - newRange * (centerFreq - m_minFrequency) / freqRange;
    float newMaxFreq = centerFreq + newRange * (m_maxFrequency - centerFreq) / freqRange;

    // Limitar rangos
    newMinFreq = qMax(1.0f, newMinFreq);
    newMaxFreq = qMin((float)m_sampleRate / 2.0f, newMaxFreq);

    if (newMaxFreq > newMinFreq) {
        setFrequencyRange(newMinFreq, newMaxFreq);
    }

    event->accept();
}

void SpectrogramWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
}

void SpectrogramWidget::updateDisplay()
{
    if (m_imageNeedsUpdate) {
        update();
    }
}

void SpectrogramWidget::showContextMenu(const QPoint& pos)
{
    if (m_contextMenu) {
        m_contextMenu->exec(mapToGlobal(pos));
    }
}

void SpectrogramWidget::saveImage(const QString& filename)
{
    QImage image(size(), QImage::Format_RGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Renderizar todo el widget
    renderSpectrogram(painter);
    if (m_showGrid) renderGrid(painter);
    if (m_showFrequencyLabels) renderFrequencyLabels(painter);
    if (m_showTimeLabels) renderTimeLabels(painter);
    if (m_showColorBar) renderColorBar(painter);

    if (image.save(filename)) {
        qDebug() << "Image saved to:" << filename;
    } else {
        QMessageBox::warning(this, "Error", "Failed to save image to " + filename);
    }
}

void SpectrogramWidget::copyToClipboard()
{
    QImage image(size(), QImage::Format_RGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Renderizar todo el widget
    renderSpectrogram(painter);
    if (m_showGrid) renderGrid(painter);
    if (m_showFrequencyLabels) renderFrequencyLabels(painter);
    if (m_showTimeLabels) renderTimeLabels(painter);
    if (m_showColorBar) renderColorBar(painter);

    QApplication::clipboard()->setImage(image);
    qDebug() << "Image copied to clipboard";
}

void SpectrogramWidget::updateGeometry()
{
    // Recalcular geometría si es necesario
    update();
}
