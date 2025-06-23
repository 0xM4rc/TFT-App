#ifndef AUDIO_STATISTICS_H
#define AUDIO_STATISTICS_H

#include "qaudioformat.h"

struct AudioStatistics {
    // Estadísticas de procesamiento
    double processingLoad = 0.0;        // Carga de procesamiento (%)
    qint64 totalSamples = 0;            // Total de muestras procesadas
    double averageLatency = 0.0;        // Latencia promedio (ms)

    // Estadísticas de buffer
    int bufferSize = 0;                 // Tamaño actual del buffer
    int bufferUtilization = 0;          // Utilización del buffer (%)
    int bufferOverruns = 0;             // Número de overruns del buffer

    // Estadísticas de tiempo
    qint64 processingTime = 0;          // Tiempo total de procesamiento (μs)
    qint64 chunksProcessed = 0;         // Chunks procesados
    double avgChunkTime = 0.0;          // Tiempo promedio por chunk (μs)

    // Estadísticas de audio
    QAudioFormat currentFormat;         // Formato actual
    bool formatValid = false;           // Si el formato es válido

    // Método para calcular estadísticas derivadas
    void updateDerivedStats() {
        if (chunksProcessed > 0) {
            avgChunkTime = (double)processingTime / chunksProcessed;
        }

        if (bufferSize > 0) {
            // Calcular utilización del buffer basado en algún tamaño máximo
            // Este cálculo depende de la implementación específica
        }
    }

    // Método para resetear estadísticas
    void reset() {
        processingLoad = 0.0;
        totalSamples = 0;
        averageLatency = 0.0;
        bufferSize = 0;
        bufferUtilization = 0;
        bufferOverruns = 0;
        processingTime = 0;
        chunksProcessed = 0;
        avgChunkTime = 0.0;
        formatValid = false;
        currentFormat = QAudioFormat();
    }
};

#endif // AUDIO_STATISTICS_H
