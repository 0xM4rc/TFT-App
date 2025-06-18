#include "include/audio_visualizer.h"
#include "include/spectrogram_widget.h"
#include "include/waveform_widget.h"
#include "include/audio_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QTimer>
#include <QContextMenuEvent>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QTabWidget>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QDebug>

// Constantes estáticas
const double AudioVisualizer::DEFAULT_SPLITTER_RATIO = 0.4;
const QString AudioVisualizer::SETTINGS_GROUP = "AudioVisualizer";
const QString AudioVisualizer::SETTINGS_LAYOUT_MODE = "LayoutMode";
const QString AudioVisualizer::SETTINGS_SPLITTER_RATIO = "SplitterRatio";
const QString AudioVisualizer::SETTINGS_SHOW_CONTROLS = "ShowControls";
const QString AudioVisualizer::SETTINGS_SHOW_STATUS_BAR = "ShowStatusBar";
const QString AudioVisualizer::SETTINGS_AUTO_UPDATE = "AutoUpdate";
const QString AudioVisualizer::SETTINGS_MAX_FPS = "MaxFPS";

AudioVisualizer::AudioVisualizer(QWidget *parent)
    : QWidget(parent)
    , m_audioManager(nullptr)
    , m_spectrogramWidget(nullptr)
    , m_waveformWidget(nullptr)
    , m_mainLayout(nullptr)
    , m_mainSplitter(nullptr)
    , m_controlsWidget(nullptr)
    , m_statusWidget(nullptr)
    , m_spectrogramGroup(nullptr)
    , m_waveformGroup(nullptr)
    , m_layoutMode(LayoutMode::Horizontal)
    , m_splitterRatio(DEFAULT_SPLITTER_RATIO)
    , m_showControls(true)
    , m_showStatusBar(true)
    , m_showStatistics(true)
    , m_autoUpdate(true)
    , m_maxFPS(DEFAULT_MAX_FPS)
    , m_isPaused(false)
    , m_updateTimer(new QTimer(this))
    , m_statisticsTimer(new QTimer(this))
    , m_frameCount(0)
    , m_currentFPS(0.0)
    , m_lastUpdateTime(0.0)
    , m_lastStatsUpdate(0)
    , m_settings(nullptr)
    , m_settingsGroup(SETTINGS_GROUP)
{
    setupUI();
    setupConnections();
    loadSettings();

    // Configurar timers
    m_updateTimer->setSingleShot(false);
    m_statisticsTimer->setInterval(STATISTICS_UPDATE_INTERVAL);
    m_statisticsTimer->setSingleShot(false);

    connect(m_updateTimer, &QTimer::timeout, this, &AudioVisualizer::updateStatistics);
    connect(m_statisticsTimer, &QTimer::timeout, this, &AudioVisualizer::updateStatusBar);

    m_fpsTimer.start();
    m_statisticsTimer->start();

    setMinimumSize(800, 600);
}

AudioVisualizer::~AudioVisualizer()
{
    saveSettings();
    if (m_settings) {
        delete m_settings;
    }
}

void AudioVisualizer::setupUI()
{
    // Crear widgets principales
    m_spectrogramWidget = new SpectrogramWidget(this);
    m_waveformWidget = new WaveformWidget(this);

    // Crear layout principal
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(4, 4, 4, 4);
    m_mainLayout->setSpacing(4);

    setupControls();
    setupStatusBar();
    setupContextMenu();

    // Configurar layout inicial
    updateLayout();
}

void AudioVisualizer::setupControls()
{
    m_controlsWidget = new QWidget(this);
    m_controlsWidget->setMaximumHeight(120);

    auto* controlsLayout = new QVBoxLayout(m_controlsWidget);
    controlsLayout->setContentsMargins(4, 4, 4, 4);
    controlsLayout->setSpacing(4);

    // Fila 1: Controles de layout
    auto* layoutRow = new QHBoxLayout();
    layoutRow->addWidget(new QLabel("Layout:", this));

    m_layoutModeCombo = new QComboBox(this);
    m_layoutModeCombo->addItems({"Horizontal", "Vertical", "Solo Espectrograma", "Solo Waveform", "Pestañas"});
    layoutRow->addWidget(m_layoutModeCombo);

    layoutRow->addWidget(new QLabel("División:", this));
    m_splitterSlider = new QSlider(Qt::Horizontal, this);
    m_splitterSlider->setRange(10, 90);
    m_splitterSlider->setValue(static_cast<int>(m_splitterRatio * 100));
    layoutRow->addWidget(m_splitterSlider);

    layoutRow->addStretch();
    controlsLayout->addLayout(layoutRow);

    // Fila 2: Controles de espectrograma y waveform
    auto* settingsRow = new QHBoxLayout();

    // Grupo espectrograma
    auto* spectrogramGroup = new QGroupBox("Espectrograma", this);
    auto* spectrogramLayout = new QHBoxLayout(spectrogramGroup);

    spectrogramLayout->addWidget(new QLabel("Mapa:", this));
    m_spectrogramColorMapCombo = new QComboBox(this);
    m_spectrogramColorMapCombo->addItems({"Jet", "Hot", "Cool", "Escala de Grises", "Viridis", "Plasma", "Inferno"});
    spectrogramLayout->addWidget(m_spectrogramColorMapCombo);

    m_spectrogramGridCheck = new QCheckBox("Rejilla", this);
    spectrogramLayout->addWidget(m_spectrogramGridCheck);

    m_spectrogramLabelsCheck = new QCheckBox("Etiquetas", this);
    spectrogramLayout->addWidget(m_spectrogramLabelsCheck);

    settingsRow->addWidget(spectrogramGroup);

    // Grupo waveform
    auto* waveformGroup = new QGroupBox("Forma de Onda", this);
    auto* waveformLayout = new QHBoxLayout(waveformGroup);

    waveformLayout->addWidget(new QLabel("Modo:", this));
    m_waveformModeCombo = new QComboBox(this);
    m_waveformModeCombo->addItems({"Línea", "Relleno", "Barras"});
    waveformLayout->addWidget(m_waveformModeCombo);

    m_waveformAutoScaleCheck = new QCheckBox("Auto Escala", this);
    waveformLayout->addWidget(m_waveformAutoScaleCheck);

    m_waveformGridCheck = new QCheckBox("Rejilla", this);
    waveformLayout->addWidget(m_waveformGridCheck);

    settingsRow->addWidget(waveformGroup);
    controlsLayout->addLayout(settingsRow);

    // Fila 3: Controles generales
    auto* generalRow = new QHBoxLayout();

    m_autoUpdateCheck = new QCheckBox("Auto Actualizar", this);
    m_autoUpdateCheck->setChecked(m_autoUpdate);
    generalRow->addWidget(m_autoUpdateCheck);

    generalRow->addWidget(new QLabel("FPS Max:", this));
    m_maxFPSSpin = new QSpinBox(this);
    m_maxFPSSpin->setRange(1, 120);
    m_maxFPSSpin->setValue(m_maxFPS);
    generalRow->addWidget(m_maxFPSSpin);

    m_clearButton = new QPushButton("Limpiar", this);
    generalRow->addWidget(m_clearButton);

    m_screenshotButton = new QPushButton("Captura", this);
    generalRow->addWidget(m_screenshotButton);

    generalRow->addStretch();
    controlsLayout->addLayout(generalRow);

    m_mainLayout->addWidget(m_controlsWidget);
}

void AudioVisualizer::setupStatusBar()
{
    m_statusWidget = new QWidget(this);
    m_statusWidget->setMaximumHeight(30);

    auto* statusLayout = new QHBoxLayout(m_statusWidget);
    statusLayout->setContentsMargins(4, 2, 4, 2);
    statusLayout->setSpacing(8);

    // Labels de información

    m_fpsLabel = new QLabel("FPS: --", this);
    statusLayout->addWidget(m_fpsLabel);

    statusLayout->addWidget(new QLabel("|", this));

    m_cpuLoadLabel = new QLabel("CPU: --", this);
    statusLayout->addWidget(m_cpuLoadLabel);

    m_cpuProgressBar = new QProgressBar(this);
    m_cpuProgressBar->setMaximumWidth(100);
    m_cpuProgressBar->setMaximumHeight(16);
    m_cpuProgressBar->setRange(0, 100);
    statusLayout->addWidget(m_cpuProgressBar);

    statusLayout->addWidget(new QLabel("|", this));

    m_samplesLabel = new QLabel("Muestras: --", this);
    statusLayout->addWidget(m_samplesLabel);

    statusLayout->addWidget(new QLabel("|", this));

    m_latencyLabel = new QLabel("Latencia: --", this);
    statusLayout->addWidget(m_latencyLabel);

    statusLayout->addStretch();

    m_mainLayout->addWidget(m_statusWidget);
}

void AudioVisualizer::setupContextMenu()
{
    m_contextMenu = new QMenu(this);

    m_actionSaveScreenshot = new QAction("Guardar Captura...", this);
    m_contextMenu->addAction(m_actionSaveScreenshot);

    m_actionClearVisualization = new QAction("Limpiar Visualización", this);
    m_contextMenu->addAction(m_actionClearVisualization);

    m_contextMenu->addSeparator();

    m_actionShowControls = new QAction("Mostrar Controles", this);
    m_actionShowControls->setCheckable(true);
    m_actionShowControls->setChecked(m_showControls);
    m_contextMenu->addAction(m_actionShowControls);

    m_actionShowStatusBar = new QAction("Mostrar Barra de Estado", this);
    m_actionShowStatusBar->setCheckable(true);
    m_actionShowStatusBar->setChecked(m_showStatusBar);
    m_contextMenu->addAction(m_actionShowStatusBar);

    m_contextMenu->addSeparator();

    m_layoutSubmenu = new QMenu("Layout", this);
    auto* horizontalAction = new QAction("Horizontal", this);
    auto* verticalAction = new QAction("Vertical", this);
    auto* spectrogramOnlyAction = new QAction("Solo Espectrograma", this);
    auto* waveformOnlyAction = new QAction("Solo Waveform", this);

    m_layoutSubmenu->addAction(horizontalAction);
    m_layoutSubmenu->addAction(verticalAction);
    m_layoutSubmenu->addAction(spectrogramOnlyAction);
    m_layoutSubmenu->addAction(waveformOnlyAction);

    m_contextMenu->addMenu(m_layoutSubmenu);

    m_contextMenu->addSeparator();

    m_actionResetSettings = new QAction("Restablecer Configuración", this);
    m_contextMenu->addAction(m_actionResetSettings);
}

void AudioVisualizer::setupConnections()
{
    // Conexiones de controles
    connect(m_layoutModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioVisualizer::onLayoutModeChanged);

    connect(m_splitterSlider, &QSlider::valueChanged, this, [this](int value) {
        m_splitterRatio = value / 100.0;
        if (m_mainSplitter) {
            QList<int> sizes = m_mainSplitter->sizes();
            int total = sizes[0] + sizes[1];
            sizes[0] = static_cast<int>(total * m_splitterRatio);
            sizes[1] = total - sizes[0];
            m_mainSplitter->setSizes(sizes);
        }
    });

    connect(m_spectrogramColorMapCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioVisualizer::onSpectrogramSettingsChanged);
    connect(m_spectrogramGridCheck, &QCheckBox::toggled,
            this, &AudioVisualizer::onSpectrogramSettingsChanged);
    connect(m_spectrogramLabelsCheck, &QCheckBox::toggled,
            this, &AudioVisualizer::onSpectrogramSettingsChanged);

    connect(m_waveformModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioVisualizer::onWaveformSettingsChanged);
    connect(m_waveformAutoScaleCheck, &QCheckBox::toggled,
            this, &AudioVisualizer::onWaveformSettingsChanged);
    connect(m_waveformGridCheck, &QCheckBox::toggled,
            this, &AudioVisualizer::onWaveformSettingsChanged);

    connect(m_autoUpdateCheck, &QCheckBox::toggled,
            this, &AudioVisualizer::onAutoUpdateToggled);
    connect(m_maxFPSSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &AudioVisualizer::onMaxFPSChanged);

    connect(m_clearButton, &QPushButton::clicked,
            this, &AudioVisualizer::onClearVisualization);
    connect(m_screenshotButton, &QPushButton::clicked,
            this, [this]() { captureScreenshot(); });

    // Conexiones de widgets de visualización
    if (m_spectrogramWidget) {
        // FIX: esto esta mal y se arreglo para que compile
        //connect(m_spectrogramWidget, &SpectrogramWidget::frequencyClicked,
        //        this, &AudioVisualizer::spectrogramClicked);
        connect(m_spectrogramWidget, &SpectrogramWidget::frequencyClicked,
                this,
                [this](float freq) {
                    // Valores por defecto para los otros dos argumentos:
                    double fakeTime   = 0.0;   // o el que consideres
                    float  fakeValue  = 0.0f;
                    spectrogramClicked(freq, fakeTime, fakeValue);
                });

    }

    if (m_waveformWidget) {
        connect(m_waveformWidget, &WaveformWidget::positionClicked,
                this, &AudioVisualizer::waveformClicked);
    }

    // Conexiones del menú contextual
    connect(m_actionSaveScreenshot, &QAction::triggered,
            this, [this]() { captureScreenshot(); });
    connect(m_actionClearVisualization, &QAction::triggered,
            this, &AudioVisualizer::onClearVisualization);
    connect(m_actionShowControls, &QAction::toggled,
            this, &AudioVisualizer::setShowControls);
    connect(m_actionShowStatusBar, &QAction::toggled,
            this, &AudioVisualizer::setShowStatusBar);
    connect(m_actionResetSettings, &QAction::triggered,
            this, &AudioVisualizer::resetToDefaults);
}

void AudioVisualizer::setAudioManager(AudioManager* manager)
{
    if (m_audioManager) {
        disconnect(m_audioManager, nullptr, this, nullptr);
    }

    m_audioManager = manager;

    if (m_audioManager) {
        connect(m_audioManager, &AudioManager::visualizationDataReady,
                this, &AudioVisualizer::handleVisualizationData);
        // connect(m_audioManager, &AudioManager::formatChanged,
        //         this, &AudioVisualizer::handleFormatChanged);
        connect(m_audioManager, &AudioManager::processingStarted,
                this, &AudioVisualizer::handleProcessingStarted);
        connect(m_audioManager, &AudioManager::processingStopped,
                this, &AudioVisualizer::handleProcessingStopped);
        connect(m_audioManager, &AudioManager::statisticsUpdated,
                this, &AudioVisualizer::handleStatisticsUpdated);
    }
}

void AudioVisualizer::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode != mode) {
        m_layoutMode = mode;
        updateLayout();
        m_layoutModeCombo->setCurrentIndex(static_cast<int>(mode));
    }
}

void AudioVisualizer::updateLayout()
{
    // Limpiar layout actual
    if (m_mainSplitter) {
        m_mainLayout->removeWidget(m_mainSplitter);
        delete m_mainSplitter;
        m_mainSplitter = nullptr;
    }

    switch (m_layoutMode) {
    case LayoutMode::Horizontal:
        createHorizontalLayout();
        break;
    case LayoutMode::Vertical:
        createVerticalLayout();
        break;
    case LayoutMode::SpectrogramOnly:
        createSpectrogramOnlyLayout();
        break;
    case LayoutMode::WaveformOnly:
        createWaveformOnlyLayout();
        break;
    case LayoutMode::Tabbed:
        createTabbedLayout();
        break;
    }
}

void AudioVisualizer::createHorizontalLayout()
{
    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    m_waveformWidget->setParent(m_mainSplitter);
    m_spectrogramWidget->setParent(m_mainSplitter);

    m_mainSplitter->addWidget(m_waveformWidget);
    m_mainSplitter->addWidget(m_spectrogramWidget);

    // Configurar tamaños
    QList<int> sizes;
    sizes << static_cast<int>(300 * m_splitterRatio);
    sizes << static_cast<int>(300 * (1.0 - m_splitterRatio));
    m_mainSplitter->setSizes(sizes);

    m_mainLayout->insertWidget(1, m_mainSplitter);

    m_waveformWidget->show();
    m_spectrogramWidget->show();
}

void AudioVisualizer::createVerticalLayout()
{
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);

    m_waveformWidget->setParent(m_mainSplitter);
    m_spectrogramWidget->setParent(m_mainSplitter);

    m_mainSplitter->addWidget(m_waveformWidget);
    m_mainSplitter->addWidget(m_spectrogramWidget);

    // Configurar tamaños
    QList<int> sizes;
    sizes << static_cast<int>(400 * m_splitterRatio);
    sizes << static_cast<int>(400 * (1.0 - m_splitterRatio));
    m_mainSplitter->setSizes(sizes);

    m_mainLayout->insertWidget(1, m_mainSplitter);

    m_waveformWidget->show();
    m_spectrogramWidget->show();
}

void AudioVisualizer::createSpectrogramOnlyLayout()
{
    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    m_spectrogramWidget->setParent(m_mainSplitter);
    m_mainSplitter->addWidget(m_spectrogramWidget);

    m_mainLayout->insertWidget(1, m_mainSplitter);

    m_spectrogramWidget->show();
    m_waveformWidget->hide();
}

void AudioVisualizer::createWaveformOnlyLayout()
{
    m_mainSplitter = new QSplitter(Qt::Vertical, this);

    m_waveformWidget->setParent(m_mainSplitter);
    m_mainSplitter->addWidget(m_waveformWidget);

    m_mainLayout->insertWidget(1, m_mainSplitter);

    m_waveformWidget->show();
    m_spectrogramWidget->hide();
}

void AudioVisualizer::createTabbedLayout()
{
    auto* tabWidget = new QTabWidget(this);

    m_waveformWidget->setParent(tabWidget);
    m_spectrogramWidget->setParent(tabWidget);

    tabWidget->addTab(m_waveformWidget, "Forma de Onda");
    tabWidget->addTab(m_spectrogramWidget, "Espectrograma");

    m_mainLayout->insertWidget(1, tabWidget);

    m_waveformWidget->show();
    m_spectrogramWidget->show();
}

void AudioVisualizer::setSplitterRatio(double ratio)
{
    m_splitterRatio = qBound(0.1, ratio, 0.9);
    m_splitterSlider->setValue(static_cast<int>(m_splitterRatio * 100));

    if (m_mainSplitter && m_mainSplitter->count() >= 2) {
        QList<int> sizes = m_mainSplitter->sizes();
        int total = sizes[0] + sizes[1];
        sizes[0] = static_cast<int>(total * m_splitterRatio);
        sizes[1] = total - sizes[0];
        m_mainSplitter->setSizes(sizes);
    }
}

void AudioVisualizer::setShowControls(bool show)
{
    m_showControls = show;
    m_controlsWidget->setVisible(show);
    m_actionShowControls->setChecked(show);
}

void AudioVisualizer::setShowStatusBar(bool show)
{
    m_showStatusBar = show;
    m_statusWidget->setVisible(show);
    m_actionShowStatusBar->setChecked(show);
}

void AudioVisualizer::setShowStatistics(bool show)
{
    m_showStatistics = show;
    updateControlsVisibility();
}

void AudioVisualizer::setAutoUpdate(bool enabled)
{
    m_autoUpdate = enabled;
    m_autoUpdateCheck->setChecked(enabled);

    if (enabled && !m_isPaused) {
        int interval = 1000 / m_maxFPS;
        m_updateTimer->setInterval(interval);
        if (!m_updateTimer->isActive()) {
            m_updateTimer->start();
        }
    } else {
        m_updateTimer->stop();
    }
}

void AudioVisualizer::setMaxFPS(int fps)
{
    m_maxFPS = qBound(1, fps, 120);
    m_maxFPSSpin->setValue(m_maxFPS);

    if (m_autoUpdate && !m_isPaused) {
        int interval = 1000 / m_maxFPS;
        m_updateTimer->setInterval(interval);
    }
}

void AudioVisualizer::updateVisualization(const VisualizationData& data)
{
    if (m_isPaused || !m_autoUpdate) {
        return;
    }

    // Actualizar espectrograma
    if (m_spectrogramWidget && m_spectrogramWidget->isVisible()) {
        m_spectrogramWidget->updateSpectrogram(data.spectrum);
    }

    // Actualizar waveform
    if (m_waveformWidget && m_waveformWidget->isVisible()) {
        m_waveformWidget->updateWaveform(data.waveform, data.peakLevel, data.rmsLevel);
    }

    // Actualizar estadísticas de FPS
    calculateFPS();
}

void AudioVisualizer::clearVisualization()
{
    if (m_spectrogramWidget) {
        m_spectrogramWidget->clearHistory();
    }

    if (m_waveformWidget) {
        m_waveformWidget->clearWaveform();
    }

    resetStatistics();
}

void AudioVisualizer::setPaused(bool paused)
{
    m_isPaused = paused;

    if (paused) {
        m_updateTimer->stop();
    } else if (m_autoUpdate) {
        int interval = 1000 / m_maxFPS;
        m_updateTimer->setInterval(interval);
        m_updateTimer->start();
    }
}

void AudioVisualizer::captureScreenshot(const QString& filename)
{
    QPixmap screenshot = grab();

    QString saveFilename = filename;
    if (saveFilename.isEmpty()) {
        QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd_hh-mm-ss");
        saveFilename = QString("audio_visualizer_%1.png").arg(timestamp);

        saveFilename = QFileDialog::getSaveFileName(
            this,
            "Guardar Captura de Pantalla",
            saveFilename,
            "Imágenes PNG (*.png);;Imágenes JPEG (*.jpg);;Todos los archivos (*.*)"
            );
    }

    if (!saveFilename.isEmpty()) {
        if (screenshot.save(saveFilename)) {
            QMessageBox::information(this, "Captura Guardada",
                                     QString("Captura guardada en: %1").arg(saveFilename));
        } else {
            QMessageBox::warning(this, "Error", "Error al guardar la captura de pantalla.");
        }
    }
}

void AudioVisualizer::saveSettings()
{
    if (!m_settings) {
        m_settings = new QSettings(this);
    }

    m_settings->beginGroup(m_settingsGroup);
    m_settings->setValue(SETTINGS_LAYOUT_MODE, static_cast<int>(m_layoutMode));
    m_settings->setValue(SETTINGS_SPLITTER_RATIO, m_splitterRatio);
    m_settings->setValue(SETTINGS_SHOW_CONTROLS, m_showControls);
    m_settings->setValue(SETTINGS_SHOW_STATUS_BAR, m_showStatusBar);
    m_settings->setValue(SETTINGS_AUTO_UPDATE, m_autoUpdate);
    m_settings->setValue(SETTINGS_MAX_FPS, m_maxFPS);
    m_settings->endGroup();
}

void AudioVisualizer::loadSettings()
{
    if (!m_settings) {
        m_settings = new QSettings(this);
    }

    m_settings->beginGroup(m_settingsGroup);
    m_layoutMode = static_cast<LayoutMode>(m_settings->value(SETTINGS_LAYOUT_MODE,
                                                             static_cast<int>(LayoutMode::Horizontal)).toInt());
    m_splitterRatio = m_settings->value(SETTINGS_SPLITTER_RATIO, DEFAULT_SPLITTER_RATIO).toDouble();
    m_showControls = m_settings->value(SETTINGS_SHOW_CONTROLS, true).toBool();
    m_showStatusBar = m_settings->value(SETTINGS_SHOW_STATUS_BAR, true).toBool();
    m_autoUpdate = m_settings->value(SETTINGS_AUTO_UPDATE, true).toBool();
    m_maxFPS = m_settings->value(SETTINGS_MAX_FPS, DEFAULT_MAX_FPS).toInt();
    m_settings->endGroup();

    // Aplicar configuración cargada
    updateControlsVisibility();
}

void AudioVisualizer::resetToDefaults()
{
    m_layoutMode = LayoutMode::Horizontal;
    m_splitterRatio = DEFAULT_SPLITTER_RATIO;
    m_showControls = true;
    m_showStatusBar = true;
    m_showStatistics = true;
    m_autoUpdate = true;
    m_maxFPS = DEFAULT_MAX_FPS;

    // Actualizar UI
    m_layoutModeCombo->setCurrentIndex(static_cast<int>(m_layoutMode));
    m_splitterSlider->setValue(static_cast<int>(m_splitterRatio * 100));
    m_autoUpdateCheck->setChecked(m_autoUpdate);
    m_maxFPSSpin->setValue(m_maxFPS);

    updateLayout();
    updateControlsVisibility();

    QMessageBox::information(this, "Configuración", "Configuración restablecida a valores por defecto.");
}

// Slots para AudioManager
void AudioVisualizer::handleVisualizationData(const VisualizationData& data)
{
    updateVisualization(data);
}

// void AudioVisualizer::handleFormatChanged(const QAudioFormat& format)
// {
//     // Profundidad de bits = bytesPorMuestra * 8
//     const int bitsPerSample = format.bytesPerSample() * 8;

//     QString formatText = QString("%1 Hz, %2-bit, %3")
//                              .arg(format.sampleRate())
//                              .arg(bitsPerSample)
//                              .arg(format.channelCount() == 1 ? "Mono" : "Estéreo");

//     m_formatLabel->setText(tr("Formato: %1").arg(formatText));
// }


void AudioVisualizer::handleProcessingStarted()
{
    resetStatistics();
    m_fpsTimer.restart();
}

void AudioVisualizer::handleProcessingStopped()
{
    m_updateTimer->stop();
}

void AudioVisualizer::handleStatisticsUpdated(double cpuLoad, qint64 totalSamples, double avgLatency)
{
    m_cpuLoadLabel->setText(QString("CPU: %1%").arg(QString::number(cpuLoad, 'f', 1)));
    m_cpuProgressBar->setValue(static_cast<int>(cpuLoad));

    m_samplesLabel->setText(QString("Muestras: %1").arg(totalSamples));
    m_latencyLabel->setText(QString("Latencia: %1 ms").arg(QString::number(avgLatency, 'f', 1)));
}

// Slots para controles
void AudioVisualizer::onLayoutModeChanged()
{
    LayoutMode newMode = static_cast<LayoutMode>(m_layoutModeCombo->currentIndex());
    setLayoutMode(newMode);
}

void AudioVisualizer::onSpectrogramSettingsChanged()
{
    applySpectrogramSettings();
}

void AudioVisualizer::onWaveformSettingsChanged()
{
    applyWaveformSettings();
}

void AudioVisualizer::onAutoUpdateToggled(bool enabled)
{
    setAutoUpdate(enabled);
}

// NOTE: revisar y completar
// --------------------------------------

void AudioVisualizer::onMaxFPSChanged(int fps)
{
    setMaxFPS(fps);
}

void AudioVisualizer::onClearVisualization()
{
    clearVisualization();
}

void AudioVisualizer::onSaveSettings()
{
    saveSettings();
    QMessageBox::information(this, "Configuración", "Configuración guardada correctamente.");
}

void AudioVisualizer::onLoadSettings()
{
    loadSettings();
    QMessageBox::information(this, "Configuración", "Configuración cargada correctamente.");
}

void AudioVisualizer::onResetSettings()
{
    resetToDefaults();
}

// Slots para menú contextual
void AudioVisualizer::showSpectrogramContextMenu(const QPoint& pos)
{
    if (m_spectrogramWidget && m_spectrogramWidget->isVisible()) {
        QPoint globalPos = m_spectrogramWidget->mapToGlobal(pos);
        m_contextMenu->exec(globalPos);
    }
}

void AudioVisualizer::showWaveformContextMenu(const QPoint& pos)
{
    if (m_waveformWidget && m_waveformWidget->isVisible()) {
        QPoint globalPos = m_waveformWidget->mapToGlobal(pos);
        m_contextMenu->exec(globalPos);
    }
}

// Slots internos
void AudioVisualizer::updateStatistics()
{
    QMutexLocker locker(&m_statisticsMutex);

    qint64 currentTime = m_fpsTimer.elapsed();
    if (currentTime - m_lastStatsUpdate >= STATISTICS_UPDATE_INTERVAL) {
        calculateFPS();
        m_lastStatsUpdate = currentTime;

        emit statisticsChanged(m_currentFPS, m_lastUpdateTime);
    }
}

void AudioVisualizer::updateStatusBar()
{
    if (m_showStatusBar && m_statusWidget->isVisible()) {
        m_fpsLabel->setText(QString("FPS: %1").arg(QString::number(m_currentFPS, 'f', 1)));
    }
}

void AudioVisualizer::updateControlsVisibility()
{
    if (m_controlsWidget) {
        m_controlsWidget->setVisible(m_showControls);
    }

    if (m_statusWidget) {
        m_statusWidget->setVisible(m_showStatusBar);
    }

    // TODO: Implementar visibilidad condicional de estadísticas específicas
    // basada en m_showStatistics
}

// Métodos de configuración
void AudioVisualizer::applySpectrogramSettings()
{
    if (!m_spectrogramWidget) return;

    // TODO: Implementar aplicación de configuración del espectrograma
    // - Color map desde m_spectrogramColorMapCombo
    // - Grid desde m_spectrogramGridCheck
    // - Labels desde m_spectrogramLabelsCheck
    // - Frecuencia mín/máx desde m_minFreqSpin/m_maxFreqSpin
    // - Amplitud mín/máx desde m_minAmpSpin/m_maxAmpSpin

    // Ejemplo básico:
    int colorMapIndex = m_spectrogramColorMapCombo->currentIndex();
    bool showGrid = m_spectrogramGridCheck->isChecked();
    bool showLabels = m_spectrogramLabelsCheck->isChecked();

    // m_spectrogramWidget->setColorMap(colorMapIndex);
    // m_spectrogramWidget->setShowGrid(showGrid);
    // m_spectrogramWidget->setShowLabels(showLabels);
}

void AudioVisualizer::applyWaveformSettings()
{
    if (!m_waveformWidget) return;

    // TODO: Implementar aplicación de configuración del waveform
    // - Modo desde m_waveformModeCombo
    // - Auto escala desde m_waveformAutoScaleCheck
    // - Grid desde m_waveformGridCheck
    // - Peaks desde m_waveformPeaksCheck (si existe)
    // - RMS desde m_waveformRMSCheck (si existe)
    // - Duración desde m_waveformDurationSpin (si existe)
    // - Escala desde m_waveformScaleSpin (si existe)

    // Ejemplo básico:
    int mode = m_waveformModeCombo->currentIndex();
    bool autoScale = m_waveformAutoScaleCheck->isChecked();
    bool showGrid = m_waveformGridCheck->isChecked();

    // m_waveformWidget->setDisplayMode(mode);
    // m_waveformWidget->setAutoScale(autoScale);
    // m_waveformWidget->setShowGrid(showGrid);
}

void AudioVisualizer::updateWidgetSettings()
{
    applySpectrogramSettings();
    applyWaveformSettings();
}

// Métodos de estadísticas
void AudioVisualizer::calculateFPS()
{
    m_frameCount++;

    qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed >= 1000) { // Actualizar cada segundo
        m_currentFPS = (m_frameCount * 1000.0) / elapsed;
        m_frameCount = 0;
        m_fpsTimer.restart();
    }
}

void AudioVisualizer::resetStatistics()
{
    QMutexLocker locker(&m_statisticsMutex);

    m_frameCount = 0;
    m_currentFPS = 0.0;
    m_lastUpdateTime = 0.0;
    m_lastStatsUpdate = 0;
    m_fpsTimer.restart();
}

// Eventos protegidos
void AudioVisualizer::contextMenuEvent(QContextMenuEvent* event)
{
    if (m_contextMenu) {
        m_contextMenu->exec(event->globalPos());
    }
}

void AudioVisualizer::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // TODO: Ajustar tamaños de widgets internos si es necesario
    // Especialmente útil para layouts complejos
}

void AudioVisualizer::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    if (m_autoUpdate && !m_isPaused && !m_updateTimer->isActive()) {
        int interval = 1000 / m_maxFPS;
        m_updateTimer->setInterval(interval);
        m_updateTimer->start();
    }

    if (!m_statisticsTimer->isActive()) {
        m_statisticsTimer->start();
    }
}

void AudioVisualizer::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);

    // Pausar actualizaciones cuando no es visible para ahorrar recursos
    m_updateTimer->stop();
    m_statisticsTimer->stop();
}

// TODO: Implementar métodos faltantes que pueden ser necesarios:

// void AudioVisualizer::setupAdditionalControls()
// {
//     // Configurar controles adicionales como:
//     // - m_spectrogramModeCombo
//     // - m_spectrogramHistorySpin
//     // - m_minFreqSpin, m_maxFreqSpin
//     // - m_minAmpSpin, m_maxAmpSpin
//     // - m_waveformDurationSpin
//     // - m_waveformScaleSpin
//     // - m_waveformPeaksCheck
//     // - m_waveformRMSCheck
// }

// void AudioVisualizer::connectAdditionalSignals()
// {
//     // Conectar señales de controles adicionales que no están en setupConnections()
// }

// void AudioVisualizer::saveAdvancedSettings()
// {
//     // Guardar configuraciones adicionales del espectrograma y waveform
//     // que no están en saveSettings()
// }

// void AudioVisualizer::loadAdvancedSettings()
// {
//     // Cargar configuraciones adicionales del espectrograma y waveform
//     // que no están en loadSettings()
// }

// void AudioVisualizer::validateSettings()
// {
//     // Validar que los valores de configuración estén en rangos válidos
//     m_splitterRatio = qBound(0.1, m_splitterRatio, 0.9);
//     m_maxFPS = qBound(1, m_maxFPS, 120);
// }

// void AudioVisualizer::handleError(const QString& errorMessage)
// {
//     // Manejar errores de los widgets de visualización
//     qDebug() << "AudioVisualizer Error:" << errorMessage;
//     QMessageBox::warning(this, "Error de Visualización", errorMessage);
// }
