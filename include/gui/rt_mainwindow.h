#ifndef RT_MAINWINDOW_H
#define RT_MAINWINDOW_H

#include <QMainWindow>

class ControlPanel;
class WaveformWidget;
class SpectrogramWidget;

/**
 * @brief Ventana principal puramente visual (sin lógica ni conexiones).
 */
class RTMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit RTMainWindow(QWidget* parent = nullptr);
    ~RTMainWindow() override = default;

private:
    void setupUi();

    // Componentes UI
    ControlPanel*      m_controlPanel{nullptr};
    WaveformWidget*    m_waveformWidget{nullptr};
    SpectrogramWidget* m_spectrogramWidget{nullptr};
};

#endif // RT_MAINWINDOW_H
