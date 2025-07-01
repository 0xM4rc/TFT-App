#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QAudioSink>
#include <QAudioFormat>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QIODevice>

#include "audio_db.h"
#include "audio_receiver.h"
#include "waveform_widget.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(AudioDb* db, QWidget* parent = nullptr);
    ~MainWindow();

private slots:
    void switchToStream();
    void switchToMicrophone();
    void onChunk(const QVector<float>& samples, qint64 timestamp);

private:
    // UI
    QWidget* central;
    QPushButton* btnStream;
    QPushButton* btnMic;
    QLabel* status;

    // Audio playback (for streaming)
    QMediaPlayer* player;
    QAudioOutput* audioOutput;

    // Audio capture (for microphone)
    AudioReceiver* micReceiver;
    QAudioSink* audioSink;
    QIODevice* sinkDevice;

    // Audio format
    QAudioFormat m_format;

    AudioDb*        m_db;
    WaveformWidget* m_waveformWidget;
};

#endif // MAINWINDOW_H
