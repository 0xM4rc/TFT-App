#ifndef FILE_AUDIO_DATA_H
#define FILE_AUDIO_DATA_H
#include <QVector>

/**
 * @brief Estructura para almacenar audio multicanal en formato PCM float
 *
 * Esta estructura maneja audio en formato flotante normalizado (-1.0 a 1.0)
 * con soporte para múltiples canales de audio.
 */
struct FileAudioData {
    QVector<QVector<float>> channels;  ///< Datos por canal: channels[canal][muestra]
    int sampleRate = 0;                ///< Frecuencia de muestreo en Hz

    /**
     * @brief Convierte el audio multicanal a mono mediante promediado
     * @return Vector con las muestras mono, o vacío si no hay datos
     */
    QVector<float> toMono() const {
        if (channels.isEmpty()) return {};

        const int frames = channels[0].size();
        const int ch = channels.size();
        QVector<float> mono(frames);

        // Si ya es mono, devolver directamente
        if (ch == 1) {
            return channels[0];
        }

        // Optimización: multiplicar es más rápido que dividir
        const float invCh = 1.0f / ch;
        for (int i = 0; i < frames; ++i) {
            float sum = 0.0f;
            for (int c = 0; c < ch; ++c) {
                sum += channels[c][i];
            }
            mono[i] = sum * invCh;
        }
        return mono;
    }

    /**
     * @brief Convierte a estéreo desde mono o ajusta canales existentes
     * @return Audio estéreo (duplica mono o mezcla multicanal)
     */
    FileAudioData toStereo() const {
        FileAudioData stereo;
        stereo.sampleRate = sampleRate;

        if (channels.isEmpty()) return stereo;

        stereo.channels.resize(2);

        if (channels.size() == 1) {
            // Mono -> Estéreo: duplicar canal
            stereo.channels[0] = channels[0];
            stereo.channels[1] = channels[0];
        } else if (channels.size() == 2) {
            // Ya es estéreo
            stereo.channels[0] = channels[0];
            stereo.channels[1] = channels[1];
        } else {
            // Multicanal -> Estéreo: mezclar
            const int frames = channels[0].size();
            stereo.channels[0].resize(frames);
            stereo.channels[1].resize(frames);

            // Canales impares -> izquierda, pares -> derecha
            for (int i = 0; i < frames; ++i) {
                float left = 0.0f, right = 0.0f;
                int leftCount = 0, rightCount = 0;

                for (int c = 0; c < channels.size(); ++c) {
                    if (c % 2 == 0) {
                        left += channels[c][i];
                        leftCount++;
                    } else {
                        right += channels[c][i];
                        rightCount++;
                    }
                }

                stereo.channels[0][i] = leftCount > 0 ? left / leftCount : 0.0f;
                stereo.channels[1][i] = rightCount > 0 ? right / rightCount : 0.0f;
            }
        }

        return stereo;
    }

    /**
     * @brief Obtiene el número total de muestras por canal
     * @return Número de frames/muestras por canal
     */
    int frameCount() const {
        return channels.isEmpty() ? 0 : channels[0].size();
    }

    /**
     * @brief Obtiene el número de canales de audio
     * @return Número de canales
     */
    int channelCount() const {
        return channels.size();
    }

    /**
     * @brief Verifica si la estructura está vacía
     * @return true si no contiene datos de audio
     */
    bool isEmpty() const {
        return channels.isEmpty() || frameCount() == 0;
    }

    /**
     * @brief Calcula la duración del audio en segundos
     * @return Duración en segundos, o 0.0 si no hay datos válidos
     */
    double durationSeconds() const {
        return sampleRate > 0 ? static_cast<double>(frameCount()) / sampleRate : 0.0;
    }

    /**
     * @brief Limpia todos los datos de audio
     */
    void clear() {
        channels.clear();
        sampleRate = 0;
    }

    /**
     * @brief Obtiene un canal específico de forma segura
     * @param channelIndex Índice del canal (0-based)
     * @return Referencia al vector del canal, o vector vacío si el índice es inválido
     */
    const QVector<float>& getChannel(int channelIndex) const {
        static const QVector<float> empty;
        return (channelIndex >= 0 && channelIndex < channels.size()) ?
                   channels[channelIndex] : empty;
    }

    /**
     * @brief Obtiene un canal específico de forma mutable
     * @param channelIndex Índice del canal (0-based)
     * @return Referencia mutable al vector del canal, o nullptr si es inválido
     */
    QVector<float>* getChannelMutable(int channelIndex) {
        return (channelIndex >= 0 && channelIndex < channels.size()) ?
                   &channels[channelIndex] : nullptr;
    }

    /**
     * @brief Redimensiona el audio a un número específico de canales
     * @param newChannelCount Nuevo número de canales
     * @param preserveData Si true, mantiene datos existentes; si false, limpia todo
     */
    void resizeChannels(int newChannelCount, bool preserveData = true) {
        if (preserveData && !channels.isEmpty()) {
            int currentFrames = frameCount();
            channels.resize(newChannelCount);

            // Inicializar nuevos canales con ceros si es necesario
            for (int c = channelCount(); c < newChannelCount; ++c) {
                channels[c].resize(currentFrames, 0.0f);
            }
        } else {
            channels.clear();
            channels.resize(newChannelCount);
        }
    }

    /**
     * @brief Verifica si los datos de audio son válidos
     * @return true si todos los canales tienen el mismo número de muestras
     */
    bool isValid() const {
        if (channels.isEmpty() || sampleRate <= 0) return false;

        int expectedFrames = channels[0].size();
        for (int c = 1; c < channels.size(); ++c) {
            if (channels[c].size() != expectedFrames) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Normaliza el audio al nivel máximo sin saturación
     * @param targetLevel Nivel objetivo (por defecto 0.95 para evitar clipping)
     */
    void normalize(float targetLevel = 0.95f) {
        if (isEmpty()) return;

        // Encontrar el pico máximo
        float maxLevel = 0.0f;
        for (const auto& channel : channels) {
            for (float sample : channel) {
                maxLevel = qMax(maxLevel, qAbs(sample));
            }
        }

        if (maxLevel > 0.0f && maxLevel != targetLevel) {
            float scale = targetLevel / maxLevel;

            // Aplicar normalización
            for (auto& channel : channels) {
                for (float& sample : channel) {
                    sample *= scale;
                }
            }
        }
    }
};

#endif // FILE_AUDIO_DATA_H
