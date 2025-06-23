#ifndef AUDIO_CONFIGURATION_H
#define AUDIO_CONFIGURATION_H

struct AudioConfiguration {
    // Configuración de procesamiento
    int updateRateMs = 50;              // Tasa de actualización (20 FPS)
    int fftSize = 2048;                 // Tamaño de FFT
    int overlap = 1024;                 // Overlap para FFT
    int waveformSamples = 1024;         // Muestras para waveform
    double waveformDurationS = 2.0;     // Duración del waveform en segundos

    // Configuración de visualización
    int spectrogramHistory = 100;       // Frames de historial del espectrograma
    int waveformBufferSize = 8192;      // Tamaño del buffer de waveform

    // Rango de frecuencias
    float minFrequency = 20.0f;         // Frecuencia mínima (Hz)
    float maxFrequency = 20000.0f;      // Frecuencia máxima (Hz)

    // Configuración de FFT
    enum WindowType {
        Rectangle = 0,
        Hanning = 1,
        Hamming = 2,
        Blackman = 3
    } windowType = Hanning;

    // Configuración de niveles
    float minDecibels = -80.0f;         // Nivel mínimo en dB
    float maxDecibels = 0.0f;           // Nivel máximo en dB

    // Configuración de buffer
    int maxBufferSize = 1024 * 1024;    // Tamaño máximo del buffer (1MB)

    // Método para validar configuración
    bool isValid() const {
        return updateRateMs >= 10 && updateRateMs <= 1000 &&
               fftSize >= 64 && fftSize <= 8192 &&
               (fftSize & (fftSize - 1)) == 0 && // Potencia de 2
               overlap >= 0 && overlap < fftSize &&
               waveformSamples >= 64 && waveformSamples <= 8192 &&
               waveformDurationS >= 0.01 && waveformDurationS <= 10.0 &&
               spectrogramHistory > 0 && spectrogramHistory <= 1000 &&
               waveformBufferSize > 0 && waveformBufferSize <= 32768 &&
               minFrequency >= 0 && maxFrequency > minFrequency &&
               minDecibels < maxDecibels &&
               maxBufferSize > 0;
    }
};

#endif // AUDIO_CONFIGURATION_H
