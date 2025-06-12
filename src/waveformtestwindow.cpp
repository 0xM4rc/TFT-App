#include "include/waveformtestwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QColorDialog>
#include <QMessageBox>
#include <QTimer>
#include <QtMath>
#include <cstdlib>

WaveformTestWindow::WaveformTestWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUI();
    setupConnections();
    generateTestAudio();
}

// ----------------- Audio generation ---------------------------
void WaveformTestWindow::generateTestAudio()
{
    const int sampleRate = 44100;
    const double duration = 5.0;
    const int frames = int(duration * sampleRate);

    FileAudioData audio;
    audio.sampleRate = sampleRate;
    audio.channels.resize(2);

    // Left: 440 Hz sine
    audio.channels[0].resize(frames);
    for (int i = 0; i < frames; ++i) {
        double t = double(i) / sampleRate;
        audio.channels[0][i] = 0.5f * sinf(2.0f * float(M_PI) * 440.0f * t);
    }

    // Right: 880 Hz sine + noise
    audio.channels[1].resize(frames);
    for (int i = 0; i < frames; ++i) {
        double t = double(i) / sampleRate;
        float tone  = 0.3f * sinf(2.0f * float(M_PI) * 880.0f * t);
        float noise = 0.1f * (float(rand()) / RAND_MAX - 0.5f);
        audio.channels[1][i] = tone + noise;
    }

    m_waveformView->setAudioData(audio);
    updateInfo();
}

void WaveformTestWindow::generateComplexAudio()
{
    const int sampleRate = 44100;
    const double duration = 10.0;
    const int frames = int(duration * sampleRate);

    FileAudioData audio;
    audio.sampleRate = sampleRate;
    audio.channels.resize(2);

    for (int c = 0; c < 2; ++c) {
        audio.channels[c].resize(frames);
        for (int i = 0; i < frames; ++i) {
            double t = double(i) / sampleRate;
            float sample = 0.0f;
            sample += 0.3f * sinf(2.0f * float(M_PI) * 220.0f * t);
            sample += 0.2f * sinf(2.0f * float(M_PI) * 440.0f * t);
            sample += 0.1f * sinf(2.0f * float(M_PI) * 880.0f * t);
            sample *= 0.5f + 0.5f * sinf(2.0f * float(M_PI) * 2.0f * t);
            if (c == 1)
                sample += 0.05f * sinf(2.0f * float(M_PI) * 1760.0f * t);
            audio.channels[c][i] = sample;
        }
    }

    m_waveformView->setAudioData(audio);
    updateInfo();
}

// ----------------- UI callbacks -------------------------------
void WaveformTestWindow::onDisplayModeChanged(int idx)
{
    static const WaveformView::DisplayMode map[] = {
        WaveformView::Mono,
        WaveformView::Stereo,
        WaveformView::AllChannels
    };
    m_waveformView->setDisplayMode(map[idx]);
}

void WaveformTestWindow::onRenderStyleChanged(int idx)
{
    static const WaveformView::RenderStyle map[] = {
        WaveformView::Line,
        WaveformView::Filled,
        WaveformView::Outline
    };
    m_waveformView->setRenderStyle(map[idx]);
}

void WaveformTestWindow::onZoomChanged(int value)
{
    double zoom = value / 10.0; // slider 10→1000 => 1.0x→100.0x
    m_waveformView->setZoomLevel(zoom);
    m_zoomLabel->setText(QString("Zoom: %1x").arg(zoom, 0, 'f', 1));
}

void WaveformTestWindow::onScrollChanged(int value)
{
    m_waveformView->setScrollPosition(value / 100.0);
}

void WaveformTestWindow::onPlaybackPositionChanged(int value)
{
    m_waveformView->setPlaybackPosition(value / 100.0);
}

void WaveformTestWindow::startPlaybackSimulation()
{
    if (m_playbackTimer->isActive()) {
        m_playbackTimer->stop();
        m_playButton->setText("Play");
    } else {
        m_playbackTimer->start(50);
        m_playButton->setText("Stop");
    }
}

void WaveformTestWindow::updatePlaybackPosition()
{
    double pos = m_waveformView->playbackPosition();
    pos += 0.01;
    if (pos >= 1.0) pos = 0.0;
    m_waveformView->setPlaybackPosition(pos);
    m_playbackSlider->setValue(int(pos * 100));
}

void WaveformTestWindow::chooseWaveformColor()
{
    QColor c = QColorDialog::getColor(m_waveformView->waveformColor(), this);
    if (c.isValid()) m_waveformView->setWaveformColor(c);
}

void WaveformTestWindow::chooseBackgroundColor()
{
    QColor c = QColorDialog::getColor(m_waveformView->backgroundColor(), this);
    if (c.isValid()) m_waveformView->setBackgroundColor(c);
}

void WaveformTestWindow::onSelectionChanged(double s, double e)
{
    if (qFuzzyIsNull(s) && qFuzzyIsNull(e)) {
        m_selectionLabel->setText("Selection: None");
    } else {
        double dur = m_waveformView->audioDurationSeconds();
        m_selectionLabel->setText(QString("Selection: %1s - %2s (%3s)")
                                      .arg(s*dur,0,'f',3)
                                      .arg(e*dur,0,'f',3)
                                      .arg((e-s)*dur,0,'f',3));
    }
}

void WaveformTestWindow::onPlaybackPositionClicked(double pos)
{
    QMessageBox::information(this, "Playback Position",
                             QString("Clicked at: %1 s").arg(pos * m_waveformView->audioDurationSeconds(),0,'f',3));
}

void WaveformTestWindow::clearSelection()
{
    m_waveformView->clearSelection();
}

void WaveformTestWindow::updateInfo()
{
    if (m_waveformView->hasAudioData()) {
        m_infoLabel->setText(QString("Duration: %1 s | SR: %2 Hz | Channels: %3")
                                 .arg(m_waveformView->audioDurationSeconds(), 0, 'f', 2)
                                 .arg(m_waveformView->audioSampleRate())
                                 .arg(m_waveformView->audioChannelCount()));

    } else {
        m_infoLabel->setText("No audio loaded");
    }
}

// ----------------- UI setup -----------------------------------
void WaveformTestWindow::setupUI()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLay = new QVBoxLayout(central);

    // Waveform view
    m_waveformView = new WaveformView;
    m_waveformView->setMinimumHeight(300);
    mainLay->addWidget(m_waveformView, 1);

    // Info + selection labels
    m_infoLabel = new QLabel("No audio loaded");
    m_selectionLabel = new QLabel("Selection: None");
    mainLay->addWidget(m_infoLabel);
    mainLay->addWidget(m_selectionLabel);

    // Controls row
    QHBoxLayout *ctrlRow = new QHBoxLayout;
    mainLay->addLayout(ctrlRow);

    // Display group
    QGroupBox *dispGrp = new QGroupBox("Display");
    QVBoxLayout *dispLay = new QVBoxLayout(dispGrp);
    QComboBox *modeCmb   = new QComboBox; modeCmb->addItems({"Mono","Stereo","All Channels"});
    QComboBox *styleCmb  = new QComboBox; styleCmb->addItems({"Line","Filled","Outline"}); styleCmb->setCurrentIndex(1);
    dispLay->addWidget(new QLabel("Mode:")); dispLay->addWidget(modeCmb);
    dispLay->addWidget(new QLabel("Style:")); dispLay->addWidget(styleCmb);
    ctrlRow->addWidget(dispGrp);

    // Navigation group
    QGroupBox *navGrp = new QGroupBox("Navigation");
    QVBoxLayout *navLay = new QVBoxLayout(navGrp);
    m_zoomLabel = new QLabel("Zoom: 1.0x");
    QSlider *zoomSld = new QSlider(Qt::Horizontal); zoomSld->setRange(10,1000); zoomSld->setValue(10);
    QSlider *scrollSld = new QSlider(Qt::Horizontal); scrollSld->setRange(0,100);
    navLay->addWidget(m_zoomLabel);
    navLay->addWidget(zoomSld);
    navLay->addWidget(new QLabel("Scroll:")); navLay->addWidget(scrollSld);
    ctrlRow->addWidget(navGrp);

    // Playback group
    QGroupBox *playGrp = new QGroupBox("Playback");
    QVBoxLayout *playLay = new QVBoxLayout(playGrp);
    m_playButton = new QPushButton("Play");
    playLay->addWidget(m_playButton);
    m_playbackSlider = new QSlider(Qt::Horizontal); m_playbackSlider->setRange(0,100);
    playLay->addWidget(new QLabel("Position:")); playLay->addWidget(m_playbackSlider);
    ctrlRow->addWidget(playGrp);

    // Options group
    QGroupBox *optGrp = new QGroupBox("Options");
    QVBoxLayout *optLay = new QVBoxLayout(optGrp);
    QCheckBox *gridChk = new QCheckBox("Show Grid"); gridChk->setChecked(true);
    QCheckBox *timeChk = new QCheckBox("Time Markers"); timeChk->setChecked(true);
    QCheckBox *ampChk  = new QCheckBox("Amplitude Markers"); ampChk->setChecked(true);
    QCheckBox *cursorChk = new QCheckBox("Playback Cursor"); cursorChk->setChecked(true);
    optLay->addWidget(gridChk);
    optLay->addWidget(timeChk);
    optLay->addWidget(ampChk);
    optLay->addWidget(cursorChk);
    ctrlRow->addWidget(optGrp);

    // Buttons column
    QVBoxLayout *btnLay = new QVBoxLayout;
    QPushButton *genBtn  = new QPushButton("Generate Simple Audio");
    QPushButton *gen2Btn = new QPushButton("Generate Complex Audio");
    QPushButton *clrBtn  = new QPushButton("Clear Audio");
    QPushButton *clrSelBtn = new QPushButton("Clear Selection");
    QPushButton *waveColBtn = new QPushButton("Waveform Color");
    QPushButton *bgColBtn = new QPushButton("Background Color");
    btnLay->addWidget(genBtn);
    btnLay->addWidget(gen2Btn);
    btnLay->addWidget(clrBtn);
    btnLay->addWidget(clrSelBtn);
    btnLay->addWidget(waveColBtn);
    btnLay->addWidget(bgColBtn);
    ctrlRow->addLayout(btnLay);

    // Timer
    m_playbackTimer = new QTimer(this);

    // Quick connections inside setupUI
    connect(modeCmb,  &QComboBox::currentIndexChanged, this, &WaveformTestWindow::onDisplayModeChanged);
    connect(styleCmb, &QComboBox::currentIndexChanged, this, &WaveformTestWindow::onRenderStyleChanged);
    connect(zoomSld,  &QSlider::valueChanged,          this, &WaveformTestWindow::onZoomChanged);
    connect(scrollSld,&QSlider::valueChanged,          this, &WaveformTestWindow::onScrollChanged);
    connect(m_playbackSlider,&QSlider::valueChanged,   this, &WaveformTestWindow::onPlaybackPositionChanged);
    connect(m_playButton,&QPushButton::clicked,        this, &WaveformTestWindow::startPlaybackSimulation);

    connect(genBtn,  &QPushButton::clicked, this, &WaveformTestWindow::generateTestAudio);
    connect(gen2Btn, &QPushButton::clicked, this, &WaveformTestWindow::generateComplexAudio);
    connect(clrBtn,  &QPushButton::clicked, m_waveformView, &WaveformView::clearAudioData);
    connect(clrSelBtn,&QPushButton::clicked,this,&WaveformTestWindow::clearSelection);
    connect(waveColBtn,&QPushButton::clicked,this,&WaveformTestWindow::chooseWaveformColor);
    connect(bgColBtn,&QPushButton::clicked,  this,&WaveformTestWindow::chooseBackgroundColor);

    connect(gridChk,   &QCheckBox::toggled, m_waveformView, &WaveformView::setShowGrid);
    connect(timeChk,   &QCheckBox::toggled, m_waveformView, &WaveformView::setShowTimeMarkers);
    connect(ampChk,    &QCheckBox::toggled, m_waveformView, &WaveformView::setShowAmplitudeMarkers);
    connect(cursorChk, &QCheckBox::toggled, m_waveformView, &WaveformView::setShowPlaybackCursor);

    setWindowTitle("WaveformView Test Window");
    resize(1200, 650);
}

void WaveformTestWindow::setupConnections()
{
    connect(m_waveformView, &WaveformView::selectionChanged,
            this, &WaveformTestWindow::onSelectionChanged);
    connect(m_waveformView, &WaveformView::playbackPositionClicked,
            this, &WaveformTestWindow::onPlaybackPositionClicked);
    connect(m_playbackTimer, &QTimer::timeout,
            this, &WaveformTestWindow::updatePlaybackPosition);
}
