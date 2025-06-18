#ifndef AUDIO_VISUALIZER_H
#define AUDIO_VISUALIZER_H

#include "include/data_structures/visualization_data.h"
#include "qaudioformat.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QMutex>
#include <QElapsedTimer>
#include <QMenu>
#include <QAction>
#include <QSettings>

// Forward declarations
class AudioManager;
class SpectrogramWidget;
class WaveformWidget;
class QProgressBar;
class QSlider;
class QPushButton;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;

/**
 * @brief Widget principal para visualización de audio en tiempo real
 *
 * AudioVisualizer es el componente central que coordina la visualización
 * de datos de audio, proporcionando una interfaz unificada para espectrograma,
 * forma de onda y controles de configuración.
 */
class AudioVisualizer : public QWidget
{
    Q_OBJECT

public:
    /**
     * @brief Modos de layout de visualización
     */
    enum class LayoutMode {
        Horizontal,     ///< Waveform arriba, espectrograma abajo
        Vertical,       ///< Waveform izquierda, espectrograma derecha
        SpectrogramOnly,///< Solo espectrograma
        WaveformOnly,   ///< Solo waveform
        Tabbed          ///< En pestañas separadas
    };

    /**
     * @brief Constructor
     * @param parent Widget padre
     */
    explicit AudioVisualizer(QWidget *parent = nullptr);

    /**
     * @brief Destructor
     */
    ~AudioVisualizer();

    // Configuración del AudioManager
    void setAudioManager(AudioManager* manager);
    AudioManager* audioManager() const { return m_audioManager; }

    // Configuración de layout
    void setLayoutMode(LayoutMode mode);
    LayoutMode layoutMode() const { return m_layoutMode; }

    void setSplitterRatio(double ratio);
    double splitterRatio() const { return m_splitterRatio; }

    // Configuración de widgets
    void setShowControls(bool show);
    bool showControls() const { return m_showControls; }

    void setShowStatusBar(bool show);
    bool showStatusBar() const { return m_showStatusBar; }

    void setShowStatistics(bool show);
    bool showStatistics() const { return m_showStatistics; }

    // Acceso a widgets individuales
    SpectrogramWidget* spectrogramWidget() const { return m_spectrogramWidget; }
    WaveformWidget* waveformWidget() const { return m_waveformWidget; }

    // Control de actualización
    void setAutoUpdate(bool enabled);
    bool autoUpdate() const { return m_autoUpdate; }

    void setMaxFPS(int fps);
    int maxFPS() const { return m_maxFPS; }

    // Configuración persistente
    void saveSettings();
    void loadSettings();
    void resetToDefaults();

public slots:
    /**
     * @brief Actualizar visualización con nuevos datos
     * @param data Datos de visualización
     */
    void updateVisualization(const VisualizationData& data);

    /**
     * @brief Limpiar todas las visualizaciones
     */
    void clearVisualization();

    /**
     * @brief Pausar/reanudar visualización
     * @param paused Estado de pausa
     */
    void setPaused(bool paused);

    /**
     * @brief Capturar screenshot de la visualización
     * @param filename Nombre del archivo (opcional)
     */
    void captureScreenshot(const QString& filename = QString());

signals:
    /**
     * @brief Señal emitida cuando se hace click en el espectrograma
     * @param frequency Frecuencia clickeada
     * @param time Tiempo clickeado
     * @param amplitude Amplitud
     */
    void spectrogramClicked(float frequency, double time, float amplitude);

    /**
     * @brief Señal emitida cuando se hace click en el waveform
     * @param time Tiempo clickeado
     * @param amplitude Amplitud
     */
    void waveformClicked(double time, float amplitude);

    /**
     * @brief Señal emitida cuando cambian las estadísticas de visualización
     * @param fps FPS actual
     * @param updateTime Tiempo de actualización en ms
     */
    void statisticsChanged(double fps, double updateTime);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    // Slots para AudioManager
    void handleVisualizationData(const VisualizationData& data);
    void handleProcessingStarted();
    void handleProcessingStopped();
    void handleStatisticsUpdated(double cpuLoad, qint64 totalSamples, double avgLatency);

    // Slots para controles
    void onLayoutModeChanged();
    void onSpectrogramSettingsChanged();
    void onWaveformSettingsChanged();
    void onAutoUpdateToggled(bool enabled);
    void onMaxFPSChanged(int fps);
    void onClearVisualization();
    void onSaveSettings();
    void onLoadSettings();
    void onResetSettings();

    // Slots para menú contextual
    void showSpectrogramContextMenu(const QPoint& pos);
    void showWaveformContextMenu(const QPoint& pos);

    // Slots internos
    void updateStatistics();
    void updateStatusBar();
    void updateControlsVisibility();

private:
    // Métodos de inicialización
    void setupUI();
    void setupControls();
    void setupStatusBar();
    void setupContextMenu();
    void setupConnections();

    // Métodos de layout
    void createHorizontalLayout();
    void createVerticalLayout();
    void createSpectrogramOnlyLayout();
    void createWaveformOnlyLayout();
    void createTabbedLayout();
    void updateLayout();

    // Métodos de configuración
    void applySpectrogramSettings();
    void applyWaveformSettings();
    void updateWidgetSettings();

    // Métodos de estadísticas
    void calculateFPS();
    void resetStatistics();

    // Miembros principales
    AudioManager* m_audioManager;
    SpectrogramWidget* m_spectrogramWidget;
    WaveformWidget* m_waveformWidget;

    // Layout y contenedores
    QVBoxLayout* m_mainLayout;
    QSplitter* m_mainSplitter;
    QWidget* m_controlsWidget;
    QWidget* m_statusWidget;
    QGroupBox* m_spectrogramGroup;
    QGroupBox* m_waveformGroup;

    // Configuración de layout
    LayoutMode m_layoutMode;
    double m_splitterRatio;
    bool m_showControls;
    bool m_showStatusBar;
    bool m_showStatistics;

    // Controles de configuración
    QComboBox* m_layoutModeCombo;
    QSlider* m_splitterSlider;

    // Controles de espectrograma
    QComboBox* m_spectrogramColorMapCombo;
    QComboBox* m_spectrogramModeCombo;
    QSpinBox* m_spectrogramHistorySpin;
    QDoubleSpinBox* m_minFreqSpin;
    QDoubleSpinBox* m_maxFreqSpin;
    QDoubleSpinBox* m_minAmpSpin;
    QDoubleSpinBox* m_maxAmpSpin;
    QCheckBox* m_spectrogramGridCheck;
    QCheckBox* m_spectrogramLabelsCheck;

    // Controles de waveform
    QComboBox* m_waveformModeCombo;
    QDoubleSpinBox* m_waveformDurationSpin;
    QDoubleSpinBox* m_waveformScaleSpin;
    QCheckBox* m_waveformAutoScaleCheck;
    QCheckBox* m_waveformGridCheck;
    QCheckBox* m_waveformPeaksCheck;
    QCheckBox* m_waveformRMSCheck;

    // Controles generales
    QCheckBox* m_autoUpdateCheck;
    QSpinBox* m_maxFPSSpin;
    QPushButton* m_clearButton;
    QPushButton* m_screenshotButton;

    // Status bar
    QLabel* m_fpsLabel;
    QLabel* m_cpuLoadLabel;
    QLabel* m_samplesLabel;
    QLabel* m_latencyLabel;
    QProgressBar* m_cpuProgressBar;

    // Menú contextual
    QMenu* m_contextMenu;
    QAction* m_actionSaveScreenshot;
    QAction* m_actionClearVisualization;
    QAction* m_actionShowControls;
    QAction* m_actionShowStatusBar;
    QAction* m_actionResetSettings;
    QMenu* m_layoutSubmenu;

    // Control de actualización y estadísticas
    bool m_autoUpdate;
    int m_maxFPS;
    bool m_isPaused;
    QTimer* m_updateTimer;
    QTimer* m_statisticsTimer;

    // Estadísticas de rendimiento
    QElapsedTimer m_fpsTimer;
    QMutex m_statisticsMutex;
    int m_frameCount;
    double m_currentFPS;
    double m_lastUpdateTime;
    qint64 m_lastStatsUpdate;

    // Configuración persistente
    QSettings* m_settings;
    QString m_settingsGroup;

    // Constantes
    static const int DEFAULT_MAX_FPS = 60;
    static const int STATISTICS_UPDATE_INTERVAL = 1000; // ms
    static const double DEFAULT_SPLITTER_RATIO;
    static const QString SETTINGS_GROUP;
    static const QString SETTINGS_LAYOUT_MODE;
    static const QString SETTINGS_SPLITTER_RATIO;
    static const QString SETTINGS_SHOW_CONTROLS;
    static const QString SETTINGS_SHOW_STATUS_BAR;
    static const QString SETTINGS_AUTO_UPDATE;
    static const QString SETTINGS_MAX_FPS;
};

Q_DECLARE_METATYPE(AudioVisualizer::LayoutMode)

#endif // AUDIO_VISUALIZER_H
