#ifndef VISUALIZATION_DATA_H
#define VISUALIZATION_DATA_H
#include <QVector>
#include <QtGlobal>

struct VisualizationData {
    QVector<float>             waveform;     ///< Datos de forma de onda (-1.0 a 1.0)
    QVector<float>             spectrum;     ///< Magnitudes del espectro FFT
    QVector<float>             frequencies;  ///< Frecuencias correspondientes al espectro
    QVector<QVector<float>>    spectrogram;  ///< Historial de espectrograma
    qint64                     timestamp  = 0;      ///< Timestamp en ms
    int                        sampleRate = 44100;  ///< Frecuencia de muestreo
    int                        channels   = 2;      ///< Número de canales
    double                     peakLevel  = 0.0;    ///< Nivel de pico para VU meter
    double                     rmsLevel   = 0.0;    ///< Nivel RMS para VU meter

    VisualizationData() = default;
};
#endif // VISUALIZATION_DATA_H
