#ifndef SPECTROGRAM_CALCULATOR_H
#define SPECTROGRAM_CALCULATOR_H

#include <QObject>
#include <QVector>
#include <QtTypes>
#include <QString>

/**
 * @brief Tipos de ventana disponibles para el análisis espectral
 */
enum class WindowType {
    Rectangular,     ///< Ventana rectangular (sin ventana)
    Hann,           ///< Ventana de Hann
    Hamming,        ///< Ventana de Hamming
    Blackman,       ///< Ventana de Blackman
    Bartlett,       ///< Ventana de Bartlett (triangular)
    Kaiser,         ///< Ventana de Kaiser
    BlackmanHarris, ///< Ventana de Blackman-Harris
    Gaussian        ///< Ventana Gaussiana
};

/**
 * @brief Configuración para el cálculo de espectrogramas
 */
struct SpectrogramConfig {
    int fftSize = 1024;           ///< Tamaño de la FFT
    int hopSize = 512;            ///< Tamaño del salto entre ventanas
    int sampleRate = 44100;       ///< Frecuencia de muestreo
    WindowType windowType = WindowType::Hann; ///< Tipo de ventana
    double kaiserBeta = 8.0;      ///< Parámetro beta para ventana Kaiser
    double gaussianSigma = 0.4;   ///< Parámetro sigma para ventana Gaussiana
    bool logScale = true;         ///< Aplicar escala logarítmica (dB)
    float noiseFloor = -100.0f;   ///< Piso de ruido en dB

    SpectrogramConfig() = default;
    SpectrogramConfig(int fftSz, int hopSz, int sampleRt = 44100)
        : fftSize(fftSz), hopSize(hopSz), sampleRate(sampleRt) {}
};

/**
 * @brief Resultado del análisis espectral
 */
struct SpectrogramFrame {
    qint64 timestamp;             ///< Timestamp del frame
    qint64 sampleOffset;          ///< Offset en muestras
    QVector<float> magnitudes;    ///< Magnitudes espectrales
    QVector<float> frequencies;   ///< Frecuencias correspondientes
    float windowGain;             ///< Ganancia de la ventana aplicada
};

/**
 * @brief Calculadora de espectrogramas con múltiples tipos de ventana
 */
class SpectrogramCalculator : public QObject
{
    Q_OBJECT

public:
    explicit SpectrogramCalculator(const SpectrogramConfig& config, QObject* parent = nullptr);
    ~SpectrogramCalculator();

    /** Obtiene la configuración actual */
    SpectrogramConfig getConfig() const { return m_config; }

    /** Establece nueva configuración */
    void setConfig(const SpectrogramConfig& config);

    /** Calcula el espectrograma de un bloque de muestras */
    SpectrogramFrame calculateFrame(const QVector<float>& samples,
                                    qint64 timestamp = 0,
                                    qint64 sampleOffset = 0);

    /** Procesa múltiples bloques con solapamiento */
    QVector<SpectrogramFrame> processOverlapped(const QVector<float>& samples,
                                                qint64 startTimestamp = 0,
                                                qint64 startOffset = 0);

    /** Obtiene las frecuencias correspondientes a cada bin */
    QVector<float> getFrequencyBins() const;

    /** Obtiene información sobre el tipo de ventana */
    QString getWindowInfo() const;

    /** Calcula una ventana específica */
    static QVector<float> calculateWindow(WindowType type, int size,
                                          double kaiserBeta = 8.0,
                                          double gaussianSigma = 0.4);

    /** Convierte tipo de ventana a string */
    static QString windowTypeToString(WindowType type);

signals:
    void errorOccurred(const QString& error);

private:
    void updateWindow();
    void updateFrequencies();
    QVector<float> applyFFT(const QVector<float>& windowedData);
    QVector<float> applyWindow(const QVector<float>& samples);
    float calculateWindowGain(const QVector<float>& window);

    // Métodos para calcular ventanas específicas
    static QVector<float> calculateRectangular(int size);
    static QVector<float> calculateHann(int size);
    static QVector<float> calculateHamming(int size);
    static QVector<float> calculateBlackman(int size);
    static QVector<float> calculateBartlett(int size);
    static QVector<float> calculateKaiser(int size, double beta);
    static QVector<float> calculateBlackmanHarris(int size);
    static QVector<float> calculateGaussian(int size, double sigma);

private:
    SpectrogramConfig m_config;
    QVector<float> m_window;
    QVector<float> m_frequencies;
    float m_windowGain;
    bool m_windowNeedsUpdate;
    bool m_frequenciesNeedUpdate;
};

#endif // SPECTROGRAM_CALCULATOR_H
