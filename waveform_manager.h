#ifndef WAVEFORM_MANAGER_H
#define WAVEFORM_MANAGER_H

#include <QObject>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QTimer>
#include "waveform_widget.h"

class AudioDb;
class DSPWorker;

/**
 * @brief Manager que simplifica el uso del WaveformWidget
 *
 * Proporciona una interfaz unificada para:
 * - Crear y configurar el widget de forma de onda
 * - Conectar automáticamente con DSPWorker y AudioDb
 * - Proporcionar controles de usuario opcionales
 * - Manejar estados comunes (play/pause, zoom, etc.)
 */
class WaveformManager : public QObject
{
    Q_OBJECT

public:
    enum ControlsStyle {
        NoControls,      // Solo el widget, sin controles
        BasicControls,   // Controles básicos (zoom, clear)
        FullControls     // Todos los controles disponibles
    };

    explicit WaveformManager(AudioDb* db, QObject* parent = nullptr);
    ~WaveformManager();

    // --- Configuración inicial ---

    /**
     * @brief Crea el widget principal con controles opcionales
     * @param parent Widget padre
     * @param style Estilo de controles a mostrar
     * @return Widget contenedor listo para usar
     */
    QWidget* createWidget(QWidget* parent = nullptr, ControlsStyle style = BasicControls);

    /**
     * @brief Obtiene el widget de forma de onda (sin controles)
     * @param parent Widget padre
     * @return WaveformWidget listo para usar
     */
    WaveformWidget* createWaveformOnly(QWidget* parent = nullptr);

    // --- Conexiones automáticas ---

    /**
     * @brief Conecta automáticamente con un DSPWorker
     * @param worker Worker de DSP para recibir datos en tiempo real
     */
    void connectToDSPWorker(DSPWorker* worker);

    /**
     * @brief Desconecta del DSPWorker actual
     */
    void disconnectFromDSPWorker();

    // --- Control de estado ---

    /**
     * @brief Pausa/reanuda la actualización automática
     * @param paused true para pausar, false para reanudar
     */
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }

    /**
     * @brief Limpia todos los datos de la forma de onda
     */
    void clear();

    /**
     * @brief Refresca desde la base de datos
     */
    void refresh();

    // --- Configuración rápida ---

    /**
     * @brief Aplica un preset de configuración
     */
    enum ConfigPreset {
        DefaultConfig,
        HighQualityConfig,
        PerformanceConfig,
        DarkThemeConfig,
        LightThemeConfig
    };

    void applyPreset(ConfigPreset preset);

    /**
     * @brief Configuración personalizada rápida
     */
    void setZoom(float verticalZoom, int horizontalZoom);
    void setColors(const QColor& waveform, const QColor& background);
    void setUpdateRate(int intervalMs);

    // --- Acceso a objetos ---

    WaveformWidget* getWaveformWidget() const { return m_waveform; }
    WaveformConfig getCurrentConfig() const;

    // --- Información de estado ---

    QString getStatusInfo() const;
    int getTotalBlocks() const;

signals:
    /**
     * @brief Se emite cuando el usuario cambia la configuración mediante controles
     */
    void configurationChanged(const WaveformConfig& newConfig);

    /**
     * @brief Se emite cuando cambia el estado de pausa
     */
    void pausedStateChanged(bool paused);

    /**
     * @brief Se emite periódicamente con información de estado
     */
    void statusUpdated(const QString& status);

public slots:
    // Slots para controlar desde código externo
    void zoomIn();
    void zoomOut();
    void zoomFit();
    void togglePause();

private slots:
    // Slots internos para controles UI
    void onVerticalZoomChanged(int value);
    void onHorizontalZoomChanged(int value);
    void onClearClicked();
    void onRefreshClicked();
    void onPauseToggled(bool checked);
    void onConfigChanged();
    void updateStatus();

private:
    // Métodos de inicialización
    void initializeWaveform();
    void setupConnections();
    void createBasicControls(QWidget* parent);
    void createFullControls(QWidget* parent);

    // Métodos de configuración
    WaveformConfig createPresetConfig(ConfigPreset preset) const;
    void applyConfigToWidget();

private:
    AudioDb* m_db;
    DSPWorker* m_dspWorker;
    WaveformWidget* m_waveform;

    // Estado
    bool m_paused;
    WaveformConfig m_currentConfig;

    // UI Controls (creados según el estilo elegido)
    QWidget* m_controlsWidget;
    QLabel* m_statusLabel;

    // Controles básicos
    QPushButton* m_pauseButton;
    QPushButton* m_clearButton;
    QPushButton* m_refreshButton;
    QSlider* m_verticalZoomSlider;
    QSlider* m_horizontalZoomSlider;

    // Controles completos
    QCheckBox* m_showGridCheck;
    QCheckBox* m_showCenterLineCheck;
    QCheckBox* m_antiAliasingCheck;
    QSpinBox* m_updateIntervalSpin;
    QPushButton* m_resetButton;

    // Timer para actualización de estado
    QTimer* m_statusTimer;
};

#endif // WAVEFORM_MANAGER_H
