#ifndef WAVEFORMTESTWINDOW_H
#define WAVEFORMTESTWINDOW_H
#include <QMainWindow>
#include <QVector>
#include "include/waveformview.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QSlider;
class QTimer;
class QGroupBox;
class QComboBox;
class QCheckBox;
QT_END_NAMESPACE

class WaveformTestWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit WaveformTestWindow(QWidget *parent = nullptr);

private slots:
    // Audio generators
    void generateTestAudio();
    void generateComplexAudio();

    // UI callbacks
    void onDisplayModeChanged(int index);
    void onRenderStyleChanged(int index);
    void onZoomChanged(int value);
    void onScrollChanged(int value);
    void onPlaybackPositionChanged(int value);
    void startPlaybackSimulation();
    void updatePlaybackPosition();
    void chooseWaveformColor();
    void chooseBackgroundColor();

    // WaveformView signals
    void onSelectionChanged(double start, double end);
    void onPlaybackPositionClicked(double position);

    void clearSelection();
    void updateInfo();

private:
    // UI helpers
    void setupUI();
    void setupConnections();

    // Members
    WaveformView *m_waveformView = nullptr;
    QLabel       *m_infoLabel = nullptr;
    QLabel       *m_selectionLabel = nullptr;
    QLabel       *m_zoomLabel = nullptr;
    QPushButton  *m_playButton = nullptr;
    QSlider      *m_playbackSlider = nullptr;
    QTimer       *m_playbackTimer = nullptr;
};
#endif // WAVEFORMTESTWINDOW_H
