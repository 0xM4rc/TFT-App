#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QLabel>
#include <QProgressBar>
#include <QAction>
#include <QActionGroup>
#include <QTimer>
#include <QGroupBox>
#include <QGridLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QColorDialog>
#include <QFileDialog>
#include <QSettings>
#include <QCloseEvent>

#include "core/audio_db.h"
#include "core/dsp_worker.h"
#include "receivers/network_receiver.h"
#include "views/waveform_render.h"
#include "views/spectrogram_renderer.h"

QT_BEGIN_NAMESPACE
class QSplitter;
class QTabWidget;
class QGroupBox;
class Controller;
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    // Menú y acciones
    void newSession();
    void openSession();
    void saveSession();
    void exportData();
    void showSettings();
    void showAbout();
    void toggleFullScreen();

    // Control de reproducción
    void startStreaming();
    void stopStreaming();
    void pauseStreaming();
    void resetAnalysis();

    // Configuración en tiempo real
    void updateDSPConfig();
    void updateWaveformConfig();
    void updateSpectrogramConfig();
    void updateNetworkConfig();
    void updateDatabaseConfig();

    // Estado del sistema
    void updateStats(qint64 blocks, qint64 samples, int buffer);
    void handleNetworkError(const QString& error);
    void handleDSPError(const QString& error);

    // Configuración visual
    void selectWaveformColor();
    void selectSpectrogramColors();
    void resetToDefaults();

private:
    Controller* m_ctrl = nullptr;

    void initializeComponents();
    void setupUi();
    void setupMenuBar();
    void setupToolBar();
    void setupStatusBar();
    void setupCentralWidget();
    void setupSettingsDialog();
    void setupConnections();

    void loadSettings();
    void saveSettings();
    void applySettings();

    void updateUIFromConfig();
    void enableControls(bool enabled);

    // Componentes principales
    AudioDb* m_audioDb;
    WaveformRenderer* m_waveformRenderer;
    SpectrogramRenderer* m_spectrogramRenderer;

    // UI Principal
    QWidget* m_centralWidget;
    QSplitter* m_mainSplitter;
    QSplitter* m_visualSplitter;
    QTabWidget* m_settingsTab;

    // Menús y acciones
    QMenu* m_fileMenu;
    QMenu* m_editMenu;
    QMenu* m_viewMenu;
    QMenu* m_toolsMenu;
    QMenu* m_helpMenu;

    QAction* m_newAction;
    QAction* m_openAction;
    QAction* m_saveAction;
    QAction* m_exportAction;
    QAction* m_exitAction;
    QAction* m_settingsAction;
    QAction* m_aboutAction;
    QAction* m_fullScreenAction;

    QActionGroup* m_viewModeGroup;
    QAction* m_waveformOnlyAction;
    QAction* m_spectrogramOnlyAction;
    QAction* m_bothViewsAction;

    // Toolbar
    QToolBar* m_mainToolBar;
    QAction* m_startAction;
    QAction* m_stopAction;
    QAction* m_pauseAction;
    QAction* m_resetAction;

    // Status Bar
    QLabel* m_statusLabel;
    QLabel* m_connectionLabel;
    QLabel* m_samplesLabel;
    QLabel* m_bufferLabel;
    QProgressBar* m_bufferProgressBar;

    // Configuración - Pestañas
    QWidget* m_dspSettingsWidget;
    QWidget* m_waveformSettingsWidget;
    QWidget* m_spectrogramSettingsWidget;
    QWidget* m_networkSettingsWidget;
    QWidget* m_databaseSettingsWidget;
    QWidget* m_generalSettingsWidget;

    // DSP Settings
    QGroupBox* m_dspGroup;
    QSpinBox* m_blockSizeSpin;
    QSpinBox* m_fftSizeSpin;
    QSpinBox* m_sampleRateSpin;
    QCheckBox* m_enableSpectrumCheck;
    QCheckBox* m_enablePeaksCheck;
    QCheckBox* m_enableRMSCheck;
    QDoubleSpinBox* m_windowOverlapSpin;
    QComboBox* m_windowTypeCombo;

    // Waveform Settings
    QGroupBox* m_waveformGroup;
    QSpinBox* m_blockWidthSpin;
    QSpinBox* m_blockSpacingSpin;
    QCheckBox* m_showPeaksCheck;
    QCheckBox* m_showRMSCheck;
    QCheckBox* m_autoScaleCheck;
    QCheckBox* m_scrollingCheck;
    QSpinBox* m_updateIntervalSpin;
    QSpinBox* m_maxVisibleBlocksSpin;
    QPushButton* m_backgroundColorBtn;
    QPushButton* m_peakColorBtn;
    QPushButton* m_rmsColorBtn;
    QSlider* m_amplitudeScaleSlider;

    // Spectrogram Settings
    QGroupBox* m_spectrogramGroup;
    QSpinBox* m_specBlockWidthSpin;
    QSpinBox* m_specUpdateIntervalSpin;
    QSpinBox* m_maxColumnsSpin;
    QCheckBox* m_autoScrollCheck;
    QDoubleSpinBox* m_minDbSpin;
    QDoubleSpinBox* m_maxDbSpin;
    QComboBox* m_colorMapCombo;
    QCheckBox* m_logFreqScaleCheck;
    QDoubleSpinBox* m_minFreqSpin;
    QDoubleSpinBox* m_maxFreqSpin;

    // Network Settings
    QGroupBox* m_networkGroup;
    QLineEdit* m_urlEdit;
    QSpinBox* m_bufferSizeSpin;
    QSpinBox* m_timeoutSpin;
    QSpinBox* m_maxRetriesSpin;
    QCheckBox* m_autoReconnectCheck;
    QComboBox* m_audioFormatCombo;
    QPushButton* m_testConnectionBtn;

    // Database Settings
    QGroupBox* m_databaseGroup;
    QLineEdit* m_dbPathEdit;
    QPushButton* m_dbPathBtn;
    QCheckBox* m_enableLoggingCheck;
    QSpinBox* m_maxRecordsSpin;
    QCheckBox* m_autoBackupCheck;
    QSpinBox* m_backupIntervalSpin;
    QPushButton* m_clearDbBtn;
    QPushButton* m_vacuumDbBtn;
    QPushButton* m_backupDbBtn;

    // General Settings
    QGroupBox* m_generalGroup;
    QComboBox* m_themeCombo;
    QComboBox* m_languageCombo;
    QCheckBox* m_startMinimizedCheck;
    QCheckBox* m_rememberWindowCheck;
    QCheckBox* m_showSplashCheck;
    QSpinBox* m_maxUndoSpin;

    // Configuraciones
    DSPConfig m_dspConfig;
    WaveformConfig m_waveformConfig;
    SpectrogramConfig m_spectrogramConfig;

    // Estado
    bool m_isStreaming;
    bool m_isPaused;
    QString m_currentSession;
    QSettings* m_settings;
    QTimer* m_uiUpdateTimer;

    // Colores temporales para preview
    QColor m_tempBackgroundColor;
    QColor m_tempPeakColor;
    QColor m_tempRmsColor;
};

#endif // MAINWINDOW_H
