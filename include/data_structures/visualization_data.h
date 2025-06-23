#ifndef VISUALIZATION_DATA_H
#define VISUALIZATION_DATA_H
#include <QVector>
#include <QtGlobal>

struct VisualizationData {
    QVector<float> waveform;
    QVector<float> spectrum;
    QVector<QVector<float>> spectrogram;
    double peakLevel = 0.0;
    double rmsLevel = 0.0;
    int sampleRate = 0;
    int channels = 0;
    qint64 timestamp = 0;
    int fftSize = 0;
    float frequencyResolution = 0.0f;
};


#endif // VISUALIZATION_DATA_H
