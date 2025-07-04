#include "mainwindow.h"
#include <QApplication>
#include <QMessageBox>
#include <QFileDialog>
#include <QColorDialog>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_audioDb(nullptr)
    , m_dspWorker(nullptr)
    , m_networkReceiver(nullptr)
    , m_waveformRenderer(nullptr)
    , m_spectrogramRenderer(nullptr)
    , m_centralWidget(nullptr)
    , m_isStreaming(false)
    , m_isPaused(false)
    , m_settings(new QSettings("AudioAnalyzer", "MainApp", this))
{
    setupUi();
    setupConnections();
    loadSettings();

    // Inicializar componentes
    initializeComponents();

    // Timer para actualizar UI
    m_uiUpdateTimer = new QTimer(this);
    m_uiUpdateTimer->setInterval(100);
    connect(m_uiUpdateTimer, &QTimer::timeout, this, &MainWindow::updateUIFromConfig);

    setWindowTitle("Audio Analyzer - Live Waveform & Spectrogram");
    resize(1200, 800);
}

MainWindow::~MainWindow()
{
    if (m_isStreaming) {
        stopStreaming();
    }
    saveSettings();
}

void MainWindow::setupUi()
{
    setupMenuBar();
    setupToolBar();
    setupStatusBar();
    setupCentralWidget();
    setupSettingsDialog();
}

void MainWindow::setupMenuBar()
{
    // File Menu
    m_fileMenu = menuBar()->addMenu("&File");

    m_newAction = new QAction("&New Session", this);
    m_newAction->setShortcut(QKeySequence::New);
    m_newAction->setStatusTip("Create a new analysis session");

    m_openAction = new QAction("&Open Session", this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip("Open an existing session");

    m_saveAction = new QAction("&Save Session", this);
    m_saveAction->setShortcut(QKeySequence::Save);
    m_saveAction->setStatusTip("Save current session");

    m_exportAction = new QAction("&Export Data", this);
    m_exportAction->setShortcut(QKeySequence("Ctrl+E"));
    m_exportAction->setStatusTip("Export analysis data");

    m_exitAction = new QAction("E&xit", this);
    m_exitAction->setShortcut(QKeySequence::Quit);
    m_exitAction->setStatusTip("Exit the application");

    m_fileMenu->addAction(m_newAction);
    m_fileMenu->addAction(m_openAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_saveAction);
    m_fileMenu->addAction(m_exportAction);
    m_fileMenu->addSeparator();
    m_fileMenu->addAction(m_exitAction);

    // Edit Menu
    m_editMenu = menuBar()->addMenu("&Edit");
    m_settingsAction = new QAction("&Settings", this);
    m_settingsAction->setShortcut(QKeySequence::Preferences);
    m_settingsAction->setStatusTip("Open settings dialog");
    m_editMenu->addAction(m_settingsAction);

    // View Menu
    m_viewMenu = menuBar()->addMenu("&View");

    m_viewModeGroup = new QActionGroup(this);
    m_waveformOnlyAction = new QAction("&Waveform Only", this);
    m_waveformOnlyAction->setCheckable(true);
    m_spectrogramOnlyAction = new QAction("&Spectrogram Only", this);
    m_spectrogramOnlyAction->setCheckable(true);
    m_bothViewsAction = new QAction("&Both Views", this);
    m_bothViewsAction->setCheckable(true);
    m_bothViewsAction->setChecked(true);

    m_viewModeGroup->addAction(m_waveformOnlyAction);
    m_viewModeGroup->addAction(m_spectrogramOnlyAction);
    m_viewModeGroup->addAction(m_bothViewsAction);

    m_viewMenu->addActions(m_viewModeGroup->actions());
    m_viewMenu->addSeparator();

    m_fullScreenAction = new QAction("&Full Screen", this);
    m_fullScreenAction->setShortcut(QKeySequence::FullScreen);
    m_fullScreenAction->setCheckable(true);
    m_viewMenu->addAction(m_fullScreenAction);

    // Tools Menu
    m_toolsMenu = menuBar()->addMenu("&Tools");

    // Help Menu
    m_helpMenu = menuBar()->addMenu("&Help");
    m_aboutAction = new QAction("&About", this);
    m_aboutAction->setStatusTip("Show application information");
    m_helpMenu->addAction(m_aboutAction);
}

void MainWindow::setupToolBar()
{
    m_mainToolBar = addToolBar("Main");
    m_mainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    m_startAction = new QAction("Start", this);
    m_startAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_startAction->setStatusTip("Start streaming and analysis");

    m_stopAction = new QAction("Stop", this);
    m_stopAction->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopAction->setStatusTip("Stop streaming and analysis");
    m_stopAction->setEnabled(false);

    m_pauseAction = new QAction("Pause", this);
    m_pauseAction->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_pauseAction->setStatusTip("Pause streaming and analysis");
    m_pauseAction->setEnabled(false);

    m_resetAction = new QAction("Reset", this);
    m_resetAction->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    m_resetAction->setStatusTip("Reset analysis data");

    m_mainToolBar->addAction(m_startAction);
    m_mainToolBar->addAction(m_stopAction);
    m_mainToolBar->addAction(m_pauseAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_resetAction);
    m_mainToolBar->addSeparator();
    m_mainToolBar->addAction(m_settingsAction);
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);

    statusBar()->addPermanentWidget(new QLabel("Connection:"));
    m_connectionLabel = new QLabel("Disconnected");
    statusBar()->addPermanentWidget(m_connectionLabel);

    statusBar()->addPermanentWidget(new QLabel("Samples:"));
    m_samplesLabel = new QLabel("0");
    statusBar()->addPermanentWidget(m_samplesLabel);

    statusBar()->addPermanentWidget(new QLabel("Buffer:"));
    m_bufferLabel = new QLabel("0%");
    statusBar()->addPermanentWidget(m_bufferLabel);

    m_bufferProgressBar = new QProgressBar;
    m_bufferProgressBar->setMaximumWidth(100);
    statusBar()->addPermanentWidget(m_bufferProgressBar);
}

void MainWindow::setupCentralWidget()
{
    m_centralWidget = new QWidget;
    setCentralWidget(m_centralWidget);

    // Layout principal horizontal con márgenes pequeños
    QHBoxLayout* mainLayout = new QHBoxLayout(m_centralWidget);
    mainLayout->setContentsMargins(2, 2, 2, 2);  // Márgenes pequeños en lugar de 0
    mainLayout->setSpacing(2);                    // Espaciado pequeño en lugar de 0

    // Splitter principal
    m_mainSplitter = new QSplitter(Qt::Horizontal);
    mainLayout->addWidget(m_mainSplitter);

    // Splitter para visualizaciones (vertical)
    m_visualSplitter = new QSplitter(Qt::Vertical);

    // Crear renderers
    m_waveformRenderer = new WaveformRenderer;
    m_spectrogramRenderer = new SpectrogramRenderer;

    // Configurar políticas de tamaño
    m_waveformRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_spectrogramRenderer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    // Añadir widgets al splitter vertical
    m_visualSplitter->addWidget(m_waveformRenderer);
    m_visualSplitter->addWidget(m_spectrogramRenderer);

    // Configurar proporciones del splitter vertical (3:1 para Waveform vs Spectrogram)
    m_visualSplitter->setStretchFactor(0, 3);
    m_visualSplitter->setStretchFactor(1, 1);

    // Crear panel de configuración
    m_settingsTab = new QTabWidget;
    m_settingsTab->setMaximumWidth(350);
    m_settingsTab->setMinimumWidth(300);  // Añadir ancho mínimo
    m_settingsTab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // Añadir al splitter principal
    m_mainSplitter->addWidget(m_visualSplitter);
    m_mainSplitter->addWidget(m_settingsTab);

    // Configurar proporciones del splitter principal (4:1 en lugar de 3:1)
    m_mainSplitter->setStretchFactor(0, 4);
    m_mainSplitter->setStretchFactor(1, 1);

    // Configurar tamaños iniciales del splitter
    QList<int> sizes;
    sizes << 800 << 300;  // Tamaños iniciales más explícitos
    m_mainSplitter->setSizes(sizes);

    // Configurar opciones del splitter
    m_mainSplitter->setChildrenCollapsible(false);  // Evitar que se colapsen
    m_visualSplitter->setChildrenCollapsible(false);
}

void MainWindow::setupSettingsDialog()
{
    // DSP Settings
    m_dspSettingsWidget = new QWidget;
    QVBoxLayout* dspLayout = new QVBoxLayout(m_dspSettingsWidget);

    m_dspGroup = new QGroupBox("DSP Configuration");
    QFormLayout* dspForm = new QFormLayout(m_dspGroup);

    m_blockSizeSpin = new QSpinBox;
    m_blockSizeSpin->setRange(256, 8192);
    m_blockSizeSpin->setValue(4096);
    dspForm->addRow("Block Size:", m_blockSizeSpin);

    m_fftSizeSpin = new QSpinBox;
    m_fftSizeSpin->setRange(256, 4096);
    m_fftSizeSpin->setValue(1024);
    dspForm->addRow("FFT Size:", m_fftSizeSpin);

    m_sampleRateSpin = new QSpinBox;
    m_sampleRateSpin->setRange(8000, 192000);
    m_sampleRateSpin->setValue(44100);
    dspForm->addRow("Sample Rate:", m_sampleRateSpin);

    m_enableSpectrumCheck = new QCheckBox;
    m_enableSpectrumCheck->setChecked(true);
    dspForm->addRow("Enable Spectrum:", m_enableSpectrumCheck);

    m_enablePeaksCheck = new QCheckBox;
    m_enablePeaksCheck->setChecked(true);
    dspForm->addRow("Enable Peaks:", m_enablePeaksCheck);

    m_windowTypeCombo = new QComboBox;
    m_windowTypeCombo->addItems({"Hanning", "Hamming", "Blackman", "Rectangular"});
    dspForm->addRow("Window Type:", m_windowTypeCombo);

    QPushButton* applyDspBtn = new QPushButton("Apply DSP Settings");
    connect(applyDspBtn, &QPushButton::clicked, this, &MainWindow::updateDSPConfig);

    dspLayout->addWidget(m_dspGroup);
    dspLayout->addWidget(applyDspBtn);
    dspLayout->addStretch();

    // Waveform Settings
    m_waveformSettingsWidget = new QWidget;
    QVBoxLayout* wfLayout = new QVBoxLayout(m_waveformSettingsWidget);

    m_waveformGroup = new QGroupBox("Waveform Configuration");
    QFormLayout* wfForm = new QFormLayout(m_waveformGroup);

    m_blockWidthSpin = new QSpinBox;
    m_blockWidthSpin->setRange(1, 20);
    m_blockWidthSpin->setValue(4);
    wfForm->addRow("Block Width:", m_blockWidthSpin);

    m_blockSpacingSpin = new QSpinBox;
    m_blockSpacingSpin->setRange(0, 10);
    m_blockSpacingSpin->setValue(1);
    wfForm->addRow("Block Spacing:", m_blockSpacingSpin);

    m_showPeaksCheck = new QCheckBox;
    m_showPeaksCheck->setChecked(true);
    wfForm->addRow("Show Peaks:", m_showPeaksCheck);

    m_showRMSCheck = new QCheckBox;
    wfForm->addRow("Show RMS:", m_showRMSCheck);

    m_autoScaleCheck = new QCheckBox;
    m_autoScaleCheck->setChecked(true);
    wfForm->addRow("Auto Scale:", m_autoScaleCheck);

    m_scrollingCheck = new QCheckBox;
    m_scrollingCheck->setChecked(true);
    wfForm->addRow("Scrolling:", m_scrollingCheck);

    m_updateIntervalSpin = new QSpinBox;
    m_updateIntervalSpin->setRange(10, 1000);
    m_updateIntervalSpin->setValue(30);
    wfForm->addRow("Update Interval (ms):", m_updateIntervalSpin);

    // Color buttons
    m_backgroundColorBtn = new QPushButton("Background");
    m_peakColorBtn = new QPushButton("Peak Color");
    m_rmsColorBtn = new QPushButton("RMS Color");

    QHBoxLayout* colorLayout = new QHBoxLayout;
    colorLayout->addWidget(m_backgroundColorBtn);
    colorLayout->addWidget(m_peakColorBtn);
    colorLayout->addWidget(m_rmsColorBtn);
    wfForm->addRow("Colors:", colorLayout);

    QPushButton* applyWfBtn = new QPushButton("Apply Waveform Settings");
    connect(applyWfBtn, &QPushButton::clicked, this, &MainWindow::updateWaveformConfig);

    wfLayout->addWidget(m_waveformGroup);
    wfLayout->addWidget(applyWfBtn);
    wfLayout->addStretch();

    // Spectrogram Settings
    m_spectrogramSettingsWidget = new QWidget;
    QVBoxLayout* specLayout = new QVBoxLayout(m_spectrogramSettingsWidget);

    m_spectrogramGroup = new QGroupBox("Spectrogram Configuration");
    QFormLayout* specForm = new QFormLayout(m_spectrogramGroup);

    m_specBlockWidthSpin = new QSpinBox;
    m_specBlockWidthSpin->setRange(1, 10);
    m_specBlockWidthSpin->setValue(2);
    specForm->addRow("Block Width:", m_specBlockWidthSpin);

    m_specUpdateIntervalSpin = new QSpinBox;
    m_specUpdateIntervalSpin->setRange(10, 1000);
    m_specUpdateIntervalSpin->setValue(30);
    specForm->addRow("Update Interval (ms):", m_specUpdateIntervalSpin);

    m_maxColumnsSpin = new QSpinBox;
    m_maxColumnsSpin->setRange(100, 2000);
    m_maxColumnsSpin->setValue(400);
    specForm->addRow("Max Columns:", m_maxColumnsSpin);

    m_autoScrollCheck = new QCheckBox;
    m_autoScrollCheck->setChecked(true);
    specForm->addRow("Auto Scroll:", m_autoScrollCheck);

    m_minDbSpin = new QDoubleSpinBox;
    m_minDbSpin->setRange(-200, 0);
    m_minDbSpin->setValue(-100);
    specForm->addRow("Min dB:", m_minDbSpin);

    m_maxDbSpin = new QDoubleSpinBox;
    m_maxDbSpin->setRange(-100, 20);
    m_maxDbSpin->setValue(0);
    specForm->addRow("Max dB:", m_maxDbSpin);

    m_colorMapCombo = new QComboBox;
    m_colorMapCombo->addItems({"Hot", "Jet", "Cool", "Gray", "Viridis"});
    specForm->addRow("Color Map:", m_colorMapCombo);

    QPushButton* applySpecBtn = new QPushButton("Apply Spectrogram Settings");
    connect(applySpecBtn, &QPushButton::clicked, this, &MainWindow::updateSpectrogramConfig);

    specLayout->addWidget(m_spectrogramGroup);
    specLayout->addWidget(applySpecBtn);
    specLayout->addStretch();

    // Network Settings
    m_networkSettingsWidget = new QWidget;
    QVBoxLayout* netLayout = new QVBoxLayout(m_networkSettingsWidget);

    m_networkGroup = new QGroupBox("Network Configuration");
    QFormLayout* netForm = new QFormLayout(m_networkGroup);

    m_urlEdit = new QLineEdit;
    m_urlEdit->setText("http://stream.radioparadise.com/rock-128");
    netForm->addRow("Stream URL:", m_urlEdit);

    m_bufferSizeSpin = new QSpinBox;
    m_bufferSizeSpin->setRange(1024, 65536);
    m_bufferSizeSpin->setValue(8192);
    netForm->addRow("Buffer Size:", m_bufferSizeSpin);

    m_timeoutSpin = new QSpinBox;
    m_timeoutSpin->setRange(1000, 30000);
    m_timeoutSpin->setValue(5000);
    netForm->addRow("Timeout (ms):", m_timeoutSpin);

    m_autoReconnectCheck = new QCheckBox;
    m_autoReconnectCheck->setChecked(true);
    netForm->addRow("Auto Reconnect:", m_autoReconnectCheck);

    m_testConnectionBtn = new QPushButton("Test Connection");
    netForm->addRow("", m_testConnectionBtn);

    QPushButton* applyNetBtn = new QPushButton("Apply Network Settings");
    connect(applyNetBtn, &QPushButton::clicked, this, &MainWindow::updateNetworkConfig);

    netLayout->addWidget(m_networkGroup);
    netLayout->addWidget(applyNetBtn);
    netLayout->addStretch();

    // Database Settings
    m_databaseSettingsWidget = new QWidget;
    QVBoxLayout* dbLayout = new QVBoxLayout(m_databaseSettingsWidget);

    m_databaseGroup = new QGroupBox("Database Configuration");
    QFormLayout* dbForm = new QFormLayout(m_databaseGroup);

    QHBoxLayout* dbPathLayout = new QHBoxLayout;
    m_dbPathEdit = new QLineEdit;
    m_dbPathEdit->setText("/home/m4rc/Desktop/tft-app/TFT-App/audio_capture.db");
    m_dbPathBtn = new QPushButton("Browse");
    dbPathLayout->addWidget(m_dbPathEdit);
    dbPathLayout->addWidget(m_dbPathBtn);
    dbForm->addRow("Database Path:", dbPathLayout);

    m_enableLoggingCheck = new QCheckBox;
    m_enableLoggingCheck->setChecked(true);
    dbForm->addRow("Enable Logging:", m_enableLoggingCheck);

    m_maxRecordsSpin = new QSpinBox;
    m_maxRecordsSpin->setRange(1000, 1000000);
    m_maxRecordsSpin->setValue(100000);
    dbForm->addRow("Max Records:", m_maxRecordsSpin);

    m_clearDbBtn = new QPushButton("Clear Database");
    m_vacuumDbBtn = new QPushButton("Vacuum Database");
    m_backupDbBtn = new QPushButton("Backup Database");

    QHBoxLayout* dbBtnLayout = new QHBoxLayout;
    dbBtnLayout->addWidget(m_clearDbBtn);
    dbBtnLayout->addWidget(m_vacuumDbBtn);
    dbBtnLayout->addWidget(m_backupDbBtn);
    dbForm->addRow("Actions:", dbBtnLayout);

    QPushButton* applyDbBtn = new QPushButton("Apply Database Settings");
    connect(applyDbBtn, &QPushButton::clicked, this, &MainWindow::updateDatabaseConfig);

    dbLayout->addWidget(m_databaseGroup);
    dbLayout->addWidget(applyDbBtn);
    dbLayout->addStretch();

    // Agregar todas las pestañas
    m_settingsTab->addTab(m_dspSettingsWidget, "DSP");
    m_settingsTab->addTab(m_waveformSettingsWidget, "Waveform");
    m_settingsTab->addTab(m_spectrogramSettingsWidget, "Spectrogram");
    m_settingsTab->addTab(m_networkSettingsWidget, "Network");
    m_settingsTab->addTab(m_databaseSettingsWidget, "Database");
}

void MainWindow::setupConnections()
{
    // Menu actions
    connect(m_newAction, &QAction::triggered, this, &MainWindow::newSession);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openSession);
    connect(m_saveAction, &QAction::triggered, this, &MainWindow::saveSession);
    connect(m_exportAction, &QAction::triggered, this, &MainWindow::exportData);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_fullScreenAction, &QAction::triggered, this, &MainWindow::toggleFullScreen);

    // Toolbar actions
    connect(m_startAction, &QAction::triggered, this, &MainWindow::startStreaming);
    connect(m_stopAction, &QAction::triggered, this, &MainWindow::stopStreaming);
    connect(m_pauseAction, &QAction::triggered, this, &MainWindow::pauseStreaming);
    connect(m_resetAction, &QAction::triggered, this, &MainWindow::resetAnalysis);

    // View mode actions
    connect(m_waveformOnlyAction, &QAction::triggered, [this]() {
        m_spectrogramRenderer->hide();
        m_waveformRenderer->show();
    });
    connect(m_spectrogramOnlyAction, &QAction::triggered, [this]() {
        m_waveformRenderer->hide();
        m_spectrogramRenderer->show();
    });
    connect(m_bothViewsAction, &QAction::triggered, [this]() {
        m_waveformRenderer->show();
        m_spectrogramRenderer->show();
    });

    // Color selection buttons
    connect(m_backgroundColorBtn, &QPushButton::clicked, this, &MainWindow::selectWaveformColor);
    connect(m_peakColorBtn, &QPushButton::clicked, this, &MainWindow::selectWaveformColor);
    connect(m_rmsColorBtn, &QPushButton::clicked, this, &MainWindow::selectWaveformColor);

    // Database buttons
    connect(m_dbPathBtn, &QPushButton::clicked, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Database Path",
                                                    m_dbPathEdit->text(), "SQLite Database (*.db)");
        if (!path.isEmpty()) {
            m_dbPathEdit->setText(path);
        }
    });

    connect(m_clearDbBtn, &QPushButton::clicked, [this]() {
        if (QMessageBox::question(this, "Clear Database",
                                  "Are you sure you want to clear all database records?") == QMessageBox::Yes) {
            if (m_audioDb) {
                m_audioDb->clearDatabase();
                m_statusLabel->setText("Database cleared");
            }
        }
    });

    connect(m_testConnectionBtn, &QPushButton::clicked, [this]() {
        // Test network connection
        m_statusLabel->setText("Testing connection...");
        // Implementation would go here
    });
}

void MainWindow::initializeComponents()
{
    // Initialize database
    QString dbPath = m_dbPathEdit->text();
    QDir dir(QFileInfo(dbPath).absolutePath());
    if (!dir.exists()) dir.mkpath(".");

    m_audioDb = new AudioDb(dbPath, this);
    if (!m_audioDb->initialize()) {
        QMessageBox::critical(this, "Database Error",
                              "Failed to initialize database at: " + dbPath);
        return;
    }

    // Initialize DSP Worker
    m_dspConfig.blockSize = m_blockSizeSpin->value();
    m_dspConfig.fftSize = m_fftSizeSpin->value();
    m_dspConfig.sampleRate = m_sampleRateSpin->value();
    m_dspConfig.enableSpectrum = m_enableSpectrumCheck->isChecked();
    m_dspConfig.enablePeaks = m_enablePeaksCheck->isChecked();

    m_dspWorker = new DSPWorker(m_dspConfig, m_audioDb, this);

    // Initialize Network Receiver
    m_networkReceiver = new NetworkReceiver(this);
    m_networkReceiver->setUrl(m_urlEdit->text());

    // Configure renderers
    updateWaveformConfig();
    updateSpectrogramConfig();

    // Connect DSP signals
    connect(m_dspWorker, &DSPWorker::framesReady,
            m_waveformRenderer, &WaveformRenderer::processFrames);
    connect(m_dspWorker, &DSPWorker::framesReady,
            m_spectrogramRenderer, &SpectrogramRenderer::processFrames);
    connect(m_dspWorker, &DSPWorker::statsUpdated,
            this, &MainWindow::updateStats);
    connect(m_dspWorker, &DSPWorker::errorOccurred,
            this, &MainWindow::handleDSPError);

    // Connect network signals
    connect(m_networkReceiver, &NetworkReceiver::floatChunkReady,
            m_dspWorker, &DSPWorker::processChunk);
    connect(m_networkReceiver, &NetworkReceiver::errorOccurred,
            this, &MainWindow::handleNetworkError);
}

// Slot implementations
void MainWindow::newSession()
{
    if (m_isStreaming) {
        stopStreaming();
    }

    resetAnalysis();
    m_currentSession.clear();
    setWindowTitle("Audio Analyzer - New Session");
    m_statusLabel->setText("New session created");
}

void MainWindow::openSession()
{
    QString fileName = QFileDialog::getOpenFileName(this,
                                                    "Open Session", "", "Session Files (*.session);;All Files (*)");

    if (!fileName.isEmpty()) {
        // Load session implementation
        m_currentSession = fileName;
        setWindowTitle("Audio Analyzer - " + QFileInfo(fileName).baseName());
        m_statusLabel->setText("Session loaded: " + QFileInfo(fileName).baseName());
    }
}

void MainWindow::saveSession()
{
    QString fileName = m_currentSession;
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getSaveFileName(this,
                                                "Save Session", "", "Session Files (*.session);;All Files (*)");
    }

    if (!fileName.isEmpty()) {
        // Save session implementation
        m_currentSession = fileName;
        setWindowTitle("Audio Analyzer - " + QFileInfo(fileName).baseName());
        m_statusLabel->setText("Session saved: " + QFileInfo(fileName).baseName());
    }
}

void MainWindow::exportData()
{
    QString fileName = QFileDialog::getSaveFileName(this,
                                                    "Export Data", "", "CSV Files (*.csv);;JSON Files (*.json);;All Files (*)");

    if (!fileName.isEmpty()) {
        // Export data implementation
        m_statusLabel->setText("Data exported to: " + QFileInfo(fileName).baseName());
    }
}

void MainWindow::showSettings()
{
    // Settings are already visible in the side panel
    // This could open a separate dialog if preferred
    m_settingsTab->setVisible(!m_settingsTab->isVisible());
}

void MainWindow::showAbout()
{
    QMessageBox::about(this, "About Audio Analyzer",
                       "Audio Analyzer v1.0\n\n"
                       "Live audio streaming analysis with waveform and spectrogram visualization.\n\n"
                       "Features:\n"
                       "• Real-time DSP processing\n"
                       "• Waveform visualization\n"
                       "• Spectrogram analysis\n"
                       "• Database logging\n"
                       "• Network streaming support");
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        m_fullScreenAction->setChecked(false);
    } else {
        showFullScreen();
        m_fullScreenAction->setChecked(true);
    }
}

void MainWindow::startStreaming()
{
    if (m_isStreaming) return;

    try {
        m_networkReceiver->start();
        m_isStreaming = true;
        m_isPaused = false;

        enableControls(true);
        m_startAction->setEnabled(false);
        m_stopAction->setEnabled(true);
        m_pauseAction->setEnabled(true);

        m_connectionLabel->setText("Connecting...");
        m_statusLabel->setText("Streaming started");

        m_uiUpdateTimer->start();

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Streaming Error",
                              QString("Failed to start streaming: %1").arg(e.what()));
    }
}

void MainWindow::stopStreaming()
{
    if (!m_isStreaming) return;

    m_networkReceiver->stop();
    m_isStreaming = false;
    m_isPaused = false;

    enableControls(false);
    m_startAction->setEnabled(true);
    m_stopAction->setEnabled(false);
    m_pauseAction->setEnabled(false);

    m_connectionLabel->setText("Disconnected");
    m_statusLabel->setText("Streaming stopped");

    m_uiUpdateTimer->stop();
}

void MainWindow::pauseStreaming()
{
    if (!m_isStreaming) return;

    m_isPaused = !m_isPaused;

    if (m_isPaused) {
        m_pauseAction->setText("Resume");
        m_pauseAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
        m_statusLabel->setText("Streaming paused");
    } else {
        m_pauseAction->setText("Pause");
        m_pauseAction->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
        m_statusLabel->setText("Streaming resumed");
    }
}

void MainWindow::resetAnalysis()
{
    if (m_waveformRenderer) {
        // Reset waveform display
    }
    if (m_spectrogramRenderer) {
        // Reset spectrogram display
    }
    if (m_audioDb) {
        m_audioDb->clearDatabase();
    }

    m_statusLabel->setText("Analysis reset");
}

void MainWindow::updateDSPConfig()
{
    if (!m_dspWorker) return;

    m_dspConfig.blockSize = m_blockSizeSpin->value();
    m_dspConfig.fftSize = m_fftSizeSpin->value();
    m_dspConfig.sampleRate = m_sampleRateSpin->value();
    m_dspConfig.enableSpectrum = m_enableSpectrumCheck->isChecked();
    m_dspConfig.enablePeaks = m_enablePeaksCheck->isChecked();

    // Apply configuration to DSP worker
    // m_dspWorker->setConfig(m_dspConfig);

    m_statusLabel->setText("DSP configuration updated");
}

void MainWindow::updateWaveformConfig()
{
    if (!m_waveformRenderer) return;

    m_waveformConfig.blockWidth = m_blockWidthSpin->value();
    m_waveformConfig.blockSpacing = m_blockSpacingSpin->value();
    m_waveformConfig.showPeaks = m_showPeaksCheck->isChecked();
    m_waveformConfig.showRMS = m_showRMSCheck->isChecked();
    m_waveformConfig.autoScale = m_autoScaleCheck->isChecked();
    m_waveformConfig.scrolling = m_scrollingCheck->isChecked();
    m_waveformConfig.updateInterval = m_updateIntervalSpin->value();
    m_waveformConfig.backgroundColor = m_tempBackgroundColor.isValid() ?
                                           m_tempBackgroundColor : Qt::black;
    m_waveformConfig.peakColor = m_tempPeakColor.isValid() ?
                                     m_tempPeakColor : Qt::cyan;
    m_waveformConfig.rmsColor = m_tempRmsColor.isValid() ?
                                    m_tempRmsColor : Qt::yellow;

    m_waveformRenderer->setConfig(m_waveformConfig);
    m_statusLabel->setText("Waveform configuration updated");
}

void MainWindow::updateSpectrogramConfig()
{
    if (!m_spectrogramRenderer) return;

    m_spectrogramConfig.fftSize = m_fftSizeSpin->value();
    m_spectrogramConfig.sampleRate = m_sampleRateSpin->value();
    m_spectrogramConfig.blockWidth = m_specBlockWidthSpin->value();
    m_spectrogramConfig.updateInterval = m_specUpdateIntervalSpin->value();
    m_spectrogramConfig.maxColumns = m_maxColumnsSpin->value();
    m_spectrogramConfig.autoScroll = m_autoScrollCheck->isChecked();
    m_spectrogramConfig.minDb = m_minDbSpin->value();
    m_spectrogramConfig.maxDb = m_maxDbSpin->value();

    m_spectrogramRenderer->setConfig(m_spectrogramConfig);
    m_statusLabel->setText("Spectrogram configuration updated");
}

void MainWindow::updateNetworkConfig()
{
    if (!m_networkReceiver) return;

    m_networkReceiver->setUrl(m_urlEdit->text());
    // Apply other network settings

    m_statusLabel->setText("Network configuration updated");
}

void MainWindow::updateDatabaseConfig()
{
    // Database configuration updates
    m_statusLabel->setText("Database configuration updated");
}

void MainWindow::updateStats(qint64 blocks, qint64 samples, int buffer)
{
    m_samplesLabel->setText(QString::number(samples));
    m_bufferLabel->setText(QString("%1%").arg(buffer));
    m_bufferProgressBar->setValue(buffer);

    if (blocks % 50 == 0) {
        qDebug() << "Stats - Blocks:" << blocks << "Samples:" << samples << "Buffer:" << buffer;
    }
}

void MainWindow::handleNetworkError(const QString& error)
{
    m_connectionLabel->setText("Error");
    m_statusLabel->setText("Network error: " + error);

    QMessageBox::warning(this, "Network Error", error);

    if (m_isStreaming) {
        stopStreaming();
    }
}

void MainWindow::handleDSPError(const QString& error)
{
    m_statusLabel->setText("DSP error: " + error);

    QMessageBox::warning(this, "DSP Error", error);
}

void MainWindow::selectWaveformColor()
{
    QPushButton* btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;

    QColor currentColor = Qt::black;
    if (btn == m_backgroundColorBtn) {
        currentColor = m_tempBackgroundColor.isValid() ? m_tempBackgroundColor : Qt::black;
    } else if (btn == m_peakColorBtn) {
        currentColor = m_tempPeakColor.isValid() ? m_tempPeakColor : Qt::cyan;
    } else if (btn == m_rmsColorBtn) {
        currentColor = m_tempRmsColor.isValid() ? m_tempRmsColor : Qt::yellow;
    }

    QColor color = QColorDialog::getColor(currentColor, this, "Select Color");
    if (color.isValid()) {
        if (btn == m_backgroundColorBtn) {
            m_tempBackgroundColor = color;
        } else if (btn == m_peakColorBtn) {
            m_tempPeakColor = color;
        } else if (btn == m_rmsColorBtn) {
            m_tempRmsColor = color;
        }

        // Update button style to show color
        btn->setStyleSheet(QString("background-color: %1").arg(color.name()));
    }
}

void MainWindow::updateUIFromConfig()
{
    // Update UI elements based on current state
    if (m_isStreaming) {
        m_connectionLabel->setText("Connected");
    }
}

void MainWindow::enableControls(bool enabled)
{
    // Enable/disable controls based on streaming state
    m_settingsTab->setEnabled(!enabled);
}

void MainWindow::loadSettings()
{
    // Load settings from QSettings
    m_settings->beginGroup("MainWindow");
    restoreGeometry(m_settings->value("geometry").toByteArray());
    restoreState(m_settings->value("windowState").toByteArray());
    m_settings->endGroup();

    // Load component settings
    m_settings->beginGroup("DSP");
    m_blockSizeSpin->setValue(m_settings->value("blockSize", 4096).toInt());
    m_fftSizeSpin->setValue(m_settings->value("fftSize", 1024).toInt());
    m_sampleRateSpin->setValue(m_settings->value("sampleRate", 44100).toInt());
    m_enableSpectrumCheck->setChecked(m_settings->value("enableSpectrum", true).toBool());
    m_enablePeaksCheck->setChecked(m_settings->value("enablePeaks", true).toBool());
    m_settings->endGroup();

    m_settings->beginGroup("Network");
    m_urlEdit->setText(m_settings->value("url", "http://stream.radioparadise.com/rock-128").toString());
    m_settings->endGroup();

    m_settings->beginGroup("Database");
    m_dbPathEdit->setText(m_settings->value("path", "/home/m4rc/Desktop/tft-app/TFT-App/audio_capture.db").toString());
    m_settings->endGroup();
}

void MainWindow::saveSettings()
{
    // Save settings to QSettings
    m_settings->beginGroup("MainWindow");
    m_settings->setValue("geometry", saveGeometry());
    m_settings->setValue("windowState", saveState());
    m_settings->endGroup();

    m_settings->beginGroup("DSP");
    m_settings->setValue("blockSize", m_blockSizeSpin->value());
    m_settings->setValue("fftSize", m_fftSizeSpin->value());
    m_settings->setValue("sampleRate", m_sampleRateSpin->value());
    m_settings->setValue("enableSpectrum", m_enableSpectrumCheck->isChecked());
    m_settings->setValue("enablePeaks", m_enablePeaksCheck->isChecked());
    m_settings->endGroup();

    m_settings->beginGroup("Network");
    m_settings->setValue("url", m_urlEdit->text());
    m_settings->endGroup();

    m_settings->beginGroup("Database");
    m_settings->setValue("path", m_dbPathEdit->text());
    m_settings->endGroup();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_isStreaming) {
        stopStreaming();
    }

    saveSettings();
    event->accept();
}

void MainWindow::selectSpectrogramColors() {
    // … implementación
}

void MainWindow::resetToDefaults() {
    // … implementación
}

