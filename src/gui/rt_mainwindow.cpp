#include "include/gui/rt_mainwindow.h"
#include <QVBoxLayout>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QLabel>
#include "include/data_structures/visualization_data.h"
#include "include/audio_manager.h"
#include "include/waveform_widget.h"
#include "include/spectrogram_widget.h"
#include "include/gui/control_panel.h"

RTMainWindow::RTMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Real‑Time Audio Visualizer"));
    setMinimumSize(1024, 600);

    setupUi();

    // Estilo oscuro sencillo
    setStyleSheet(R"(
        RTMainWindow, QStatusBar {
            background-color:#1d1f21;
            color:#dcdcdc;
        }
        ControlPanel {
            background-color:#26282a;
            border:1px solid #3c3f41;
            border-radius:8px;
        }
        WaveformWidget, SpectrogramWidget {
            background-color:#000000;
        }
    )");
}

void RTMainWindow::setupUi()
{
    // ——— Layout central
    QWidget* central = new QWidget(this);
    auto* vbox       = new QVBoxLayout(central);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    // ——— Widgets puramente visuales
    m_controlPanel      = new ControlPanel(this);
    m_waveformWidget    = new WaveformWidget(this);
    m_spectrogramWidget = new SpectrogramWidget(this);

    vbox->addWidget(m_controlPanel);
    vbox->addWidget(m_waveformWidget, 2);
    vbox->addWidget(m_spectrogramWidget, 3);

    setCentralWidget(central);

    // ——— Barra de estado
    statusBar()->showMessage(tr("Ready"));
}
