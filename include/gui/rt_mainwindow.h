#ifndef RT_MAINWINDOW_H
#define RT_MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QAudioFormat>
#include <QAudioSource>
#include <QAudioSink>
#include <QMediaDevices>
#include <QTimer>
#include <QIODevice>
#include "include/waveform_widget.h"

class RTMainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit RTMainWindow(QWidget* parent = nullptr);
    ~RTMainWindow() override;

private slots:
    void onDeviceChanged(int index);
    void readAudioData();

private:
    void setupUi();

    QComboBox*            m_combo;
    QVector<QAudioDevice> m_devices;
    QAudioFormat          m_format;
    QAudioSource*         m_audioSource = nullptr;
    QAudioSink*           m_audioSink   = nullptr;
    QIODevice*            m_io          = nullptr;
    QTimer                m_readTimer;
    WaveformWidget*       m_waveform;
};

#endif // RT_MAINWINDOW_H
