#include "waveform_manager.h"
#include "audio_db.h"
#include "dsp_worker.h"
#include <QDebug>
#include <QApplication>
#include <QStyle>

WaveformManager::WaveformManager(AudioDb* db, QObject* parent)
    : QObject(parent)
    , m_db(db)
    , m_dspWorker(nullptr)
    , m_waveform(nullptr)
    , m_paused(false)
    , m_controlsWidget(nullptr)
    , m_statusLabel(nullptr)
    , m_pauseButton(nullptr)
    , m_clearButton(nullptr)
    , m_refreshButton(nullptr)
    , m_verticalZoomSlider(nullptr)
    , m_horizontalZoomSlider(nullptr)
    , m_showGridCheck(nullptr)
    , m_showCenterLineCheck(nullptr)
    , m_antiAliasingCheck(nullptr)
    , m_updateIntervalSpin(nullptr)
    , m_resetButton(nullptr)
    , m_statusTimer(new QTimer(this))
{
    if (!m_db) {
        qWarning() << "WaveformManager: AudioDb es nullptr";
    }

    // Configuración por defecto
    applyPreset(DefaultConfig);

    // Timer para actualización de estado
    m_statusTimer->setInterval(1000); // 1 segundo
    connect(m_statusTimer, &QTimer::timeout, this, &WaveformManager::updateStatus);
    m_statusTimer->start();

    qDebug() << "WaveformManager inicializado";
}

WaveformManager::~WaveformManager() {
    if (m_statusTimer) {
        m_statusTimer->stop();
    }
}

QWidget* WaveformManager::createWidget(QWidget* parent, ControlsStyle style) {
    // Crear widget principal
    QWidget* mainWidget = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(mainWidget);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Crear waveform
    if (!m_waveform) {
        initializeWaveform();
    }

    if (m_waveform->parent() != mainWidget) {
        m_waveform->setParent(mainWidget);
    }

    layout->addWidget(m_waveform, 1); // Stretch factor = 1

    // Crear controles según el estilo
    if (style == BasicControls) {
        createBasicControls(mainWidget);
        layout->addWidget(m_controlsWidget);
    } else if (style == FullControls) {
        createFullControls(mainWidget);
        layout->addWidget(m_controlsWidget);
    }

    // Label de estado (siempre presente si hay controles)
    if (style != NoControls) {
        m_statusLabel = new QLabel("Listo", mainWidget);
        m_statusLabel->setStyleSheet("QLabel { color: #888; font-size: 10px; }");
        layout->addWidget(m_statusLabel);
    }

    return mainWidget;
}

WaveformWidget* WaveformManager::createWaveformOnly(QWidget* parent) {
    if (!m_waveform) {
        initializeWaveform();
    }

    if (parent && m_waveform->parent() != parent) {
        m_waveform->setParent(parent);
    }

    return m_waveform;
}

void WaveformManager::connectToDSPWorker(DSPWorker* worker) {
    if (m_dspWorker == worker) {
        return; // Ya conectado
    }

    // Desconectar anterior si existe
    disconnectFromDSPWorker();

    m_dspWorker = worker;

    if (m_dspWorker && m_waveform) {
        // Conectar señales
        connect(m_dspWorker, &DSPWorker::minMaxReady,
                m_waveform, &WaveformWidget::onNewPeakData);

        qDebug() << "WaveformManager: conectado a DSPWorker";
    }
}

void WaveformManager::disconnectFromDSPWorker() {
    if (m_dspWorker && m_waveform) {
        disconnect(m_dspWorker, &DSPWorker::minMaxReady,
                   m_waveform, &WaveformWidget::onNewPeakData);

        qDebug() << "WaveformManager: desconectado de DSPWorker";
    }

    m_dspWorker = nullptr;
}

void WaveformManager::setPaused(bool paused) {
    if (m_paused != paused) {
        m_paused = paused;

        if (m_waveform) {
            m_waveform->setAutoUpdate(!paused);
        }

        // Actualizar botón de pausa
        if (m_pauseButton) {
            m_pauseButton->setText(paused ? "▶" : "⏸");
            m_pauseButton->setToolTip(paused ? "Reanudar" : "Pausar");
        }

        emit pausedStateChanged(paused);
        qDebug() << "WaveformManager: pausa =" << paused;
    }
}

void WaveformManager::clear() {
    if (m_waveform) {
        m_waveform->clearWaveform();
        qDebug() << "WaveformManager: datos limpiados";
    }
}

void WaveformManager::refresh() {
    if (m_waveform) {
        m_waveform->refreshFromDatabase();
        qDebug() << "WaveformManager: refrescado desde BD";
    }
}

void WaveformManager::applyPreset(ConfigPreset preset) {
    m_currentConfig = createPresetConfig(preset);
    applyConfigToWidget();
    emit configurationChanged(m_currentConfig);
}

void WaveformManager::setZoom(float verticalZoom, int horizontalZoom) {
    m_currentConfig.amplitudeScale = verticalZoom;
    m_currentConfig.pixelsPerBlock = horizontalZoom;
    applyConfigToWidget();

    // Actualizar sliders si existen
    if (m_verticalZoomSlider) {
        m_verticalZoomSlider->setValue(static_cast<int>(verticalZoom * 100));
    }
    if (m_horizontalZoomSlider) {
        m_horizontalZoomSlider->setValue(horizontalZoom);
    }
}

void WaveformManager::setColors(const QColor& waveform, const QColor& background) {
    m_currentConfig.waveformColor = waveform;
    m_currentConfig.backgroundColor = background;
    applyConfigToWidget();
    emit configurationChanged(m_currentConfig);
}

void WaveformManager::setUpdateRate(int intervalMs) {
    m_currentConfig.updateIntervalMs = intervalMs;
    applyConfigToWidget();

    if (m_updateIntervalSpin) {
        m_updateIntervalSpin->setValue(intervalMs);
    }

    emit configurationChanged(m_currentConfig);
}

WaveformConfig WaveformManager::getCurrentConfig() const {
    return m_waveform ? m_waveform->getConfig() : m_currentConfig;
}

QString WaveformManager::getStatusInfo() const {
    if (!m_waveform) {
        return "Widget no inicializado";
    }

    QString status = m_waveform->getStatusInfo();
    if (m_paused) {
        status += " [PAUSADO]";
    }
    if (m_dspWorker) {
        status += " [DSP conectado]";
    }

    return status;
}

int WaveformManager::getTotalBlocks() const {
    return m_waveform ? m_waveform->getTotalBlocks() : 0;
}

// --- Slots públicos ---

void WaveformManager::zoomIn() {
    float newZoom = m_currentConfig.amplitudeScale * 1.2f;
    setZoom(newZoom, m_currentConfig.pixelsPerBlock);
}

void WaveformManager::zoomOut() {
    float newZoom = m_currentConfig.amplitudeScale / 1.2f;
    if (newZoom < 0.1f) newZoom = 0.1f;
    setZoom(newZoom, m_currentConfig.pixelsPerBlock);
}

void WaveformManager::zoomFit() {
    setZoom(1.0f, 2); // Valores por defecto
}

void WaveformManager::togglePause() {
    setPaused(!m_paused);
}

// --- Slots privados ---

void WaveformManager::onVerticalZoomChanged(int value) {
    float zoom = value / 100.0f;
    if (zoom != m_currentConfig.amplitudeScale) {
        m_currentConfig.amplitudeScale = zoom;
        applyConfigToWidget();
        emit configurationChanged(m_currentConfig);
    }
}

void WaveformManager::onHorizontalZoomChanged(int value) {
    if (value != m_currentConfig.pixelsPerBlock) {
        m_currentConfig.pixelsPerBlock = value;
        applyConfigToWidget();
        emit configurationChanged(m_currentConfig);
    }
}

void WaveformManager::onClearClicked() {
    clear();
}

void WaveformManager::onRefreshClicked() {
    refresh();
}

void WaveformManager::onPauseToggled(bool checked) {
    setPaused(checked);
}

void WaveformManager::onConfigChanged() {
    // Recoger cambios de todos los controles
    if (m_showGridCheck) {
        m_currentConfig.showGrid = m_showGridCheck->isChecked();
    }
    if (m_showCenterLineCheck) {
        m_currentConfig.showCenterLine = m_showCenterLineCheck->isChecked();
    }
    if (m_antiAliasingCheck) {
        m_currentConfig.antiAliasing = m_antiAliasingCheck->isChecked();
    }
    if (m_updateIntervalSpin) {
        m_currentConfig.updateIntervalMs = m_updateIntervalSpin->value();
    }

    applyConfigToWidget();
    emit configurationChanged(m_currentConfig);
}

void WaveformManager::updateStatus() {
    if (m_statusLabel) {
        m_statusLabel->setText(getStatusInfo());
    }
    emit statusUpdated(getStatusInfo());
}

// --- Métodos privados ---

void WaveformManager::initializeWaveform() {
    if (m_waveform) {
        return; // Ya inicializado
    }

    m_waveform = new WaveformWidget(m_db);
    m_waveform->setConfig(m_currentConfig);

    setupConnections();

    qDebug() << "WaveformWidget inicializado por WaveformManager";
}

void WaveformManager::setupConnections() {
    if (!m_waveform) {
        return;
    }

    // Conexiones básicas ya están en el widget
    // Aquí se pueden agregar conexiones adicionales si es necesario
}

void WaveformManager::createBasicControls(QWidget* parent) {
    m_controlsWidget = new QWidget(parent);
    QHBoxLayout* layout = new QHBoxLayout(m_controlsWidget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(8);

    // Botón pausa/play
    m_pauseButton = new QPushButton("⏸", m_controlsWidget);
    m_pauseButton->setFixedSize(30, 24);
    m_pauseButton->setToolTip("Pausar/Reanudar");
    m_pauseButton->setCheckable(true);
    connect(m_pauseButton, &QPushButton::toggled, this, &WaveformManager::onPauseToggled);
    layout->addWidget(m_pauseButton);

    // Separador
    layout->addWidget(new QLabel("|", m_controlsWidget));

    // Zoom vertical
    layout->addWidget(new QLabel("V:", m_controlsWidget));
    m_verticalZoomSlider = new QSlider(Qt::Horizontal, m_controlsWidget);
    m_verticalZoomSlider->setRange(10, 500); // 0.1x a 5.0x
    m_verticalZoomSlider->setValue(static_cast<int>(m_currentConfig.amplitudeScale * 100));
    m_verticalZoomSlider->setFixedWidth(80);
    m_verticalZoomSlider->setToolTip("Zoom vertical");
    connect(m_verticalZoomSlider, &QSlider::valueChanged,
            this, &WaveformManager::onVerticalZoomChanged);
    layout->addWidget(m_verticalZoomSlider);

    // Zoom horizontal
    layout->addWidget(new QLabel("H:", m_controlsWidget));
    m_horizontalZoomSlider = new QSlider(Qt::Horizontal, m_controlsWidget);
    m_horizontalZoomSlider->setRange(1, 10);
    m_horizontalZoomSlider->setValue(m_currentConfig.pixelsPerBlock);
    m_horizontalZoomSlider->setFixedWidth(80);
    m_horizontalZoomSlider->setToolTip("Zoom horizontal");
    connect(m_horizontalZoomSlider, &QSlider::valueChanged,
            this, &WaveformManager::onHorizontalZoomChanged);
    layout->addWidget(m_horizontalZoomSlider);

    layout->addStretch(); // Espacio flexible

    // Botones de acción
    m_clearButton = new QPushButton("Limpiar", m_controlsWidget);
    m_clearButton->setFixedHeight(24);
    connect(m_clearButton, &QPushButton::clicked, this, &WaveformManager::onClearClicked);
    layout->addWidget(m_clearButton);

    m_refreshButton = new QPushButton("Actualizar", m_controlsWidget);
    m_refreshButton->setFixedHeight(24);
    connect(m_refreshButton, &QPushButton::clicked, this, &WaveformManager::onRefreshClicked);
    layout->addWidget(m_refreshButton);
}

void WaveformManager::createFullControls(QWidget* parent) {
    // Crear controles básicos primero
    createBasicControls(parent);

    // Crear panel de opciones adicionales
    QGroupBox* optionsGroup = new QGroupBox("Opciones", parent);
    QHBoxLayout* optionsLayout = new QHBoxLayout(optionsGroup);

    // Checkboxes
    m_showGridCheck = new QCheckBox("Rejilla", optionsGroup);
    m_showGridCheck->setChecked(m_currentConfig.showGrid);
    connect(m_showGridCheck, &QCheckBox::toggled, this, &WaveformManager::onConfigChanged);
    optionsLayout->addWidget(m_showGridCheck);

    m_showCenterLineCheck = new QCheckBox("Línea central", optionsGroup);
    m_showCenterLineCheck->setChecked(m_currentConfig.showCenterLine);
    connect(m_showCenterLineCheck, &QCheckBox::toggled, this, &WaveformManager::onConfigChanged);
    optionsLayout->addWidget(m_showCenterLineCheck);

    m_antiAliasingCheck = new QCheckBox("Suavizado", optionsGroup);
    m_antiAliasingCheck->setChecked(m_currentConfig.antiAliasing);
    connect(m_antiAliasingCheck, &QCheckBox::toggled, this, &WaveformManager::onConfigChanged);
    optionsLayout->addWidget(m_antiAliasingCheck);

    // Intervalo de actualización
    optionsLayout->addWidget(new QLabel("Actualización (ms):", optionsGroup));
    m_updateIntervalSpin = new QSpinBox(optionsGroup);
    m_updateIntervalSpin->setRange(10, 1000);
    m_updateIntervalSpin->setValue(m_currentConfig.updateIntervalMs);
    m_updateIntervalSpin->setFixedWidth(60);
    connect(m_updateIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WaveformManager::onConfigChanged);
    optionsLayout->addWidget(m_updateIntervalSpin);

    optionsLayout->addStretch();

    // Botón reset
    m_resetButton = new QPushButton("Reset", optionsGroup);
    connect(m_resetButton, &QPushButton::clicked, [this]() {
        applyPreset(DefaultConfig);
    });
    optionsLayout->addWidget(m_resetButton);

    // Agregar el grupo al layout principal del widget de controles
    QVBoxLayout* mainLayout = new QVBoxLayout();
    mainLayout->addWidget(m_controlsWidget);
    mainLayout->addWidget(optionsGroup);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(4);

    QWidget* fullControlsWidget = new QWidget(parent);
    fullControlsWidget->setLayout(mainLayout);

    // Reemplazar el widget de controles
    delete m_controlsWidget;
    m_controlsWidget = fullControlsWidget;
}

WaveformConfig WaveformManager::createPresetConfig(ConfigPreset preset) const {
    WaveformConfig config;

    switch (preset) {
    case DefaultConfig:
        // Los valores por defecto ya están en WaveformConfig
        break;

    case HighQualityConfig:
        config.antiAliasing = true;
        config.pixelsPerBlock = 4;
        config.updateIntervalMs = 16; // ~60 FPS
        config.amplitudeScale = 1.5f;
        break;

    case PerformanceConfig:
        config.antiAliasing = false;
        config.pixelsPerBlock = 1;
        config.updateIntervalMs = 100; // 10 FPS
        config.showGrid = false;
        break;

    case DarkThemeConfig:
        config.backgroundColor = QColor(15, 15, 20);
        config.waveformColor = QColor(0, 200, 255);
        config.centerLineColor = QColor(60, 60, 70);
        config.gridColor = QColor(30, 30, 40);
        break;

    case LightThemeConfig:
        config.backgroundColor = QColor(250, 250, 250);
        config.waveformColor = QColor(50, 100, 200);
        config.centerLineColor = QColor(150, 150, 150);
        config.gridColor = QColor(200, 200, 200);
        break;
    }

    return config;
}

void WaveformManager::applyConfigToWidget() {
    if (m_waveform) {
        m_waveform->setConfig(m_currentConfig);
    }
}
