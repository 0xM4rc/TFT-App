#ifndef AUDIO_SOURCE_INFO_H
#define AUDIO_SOURCE_INFO_H

#include <QString>
#include "include/data_structures/source_type.h"
#include "qaudioformat.h"

struct AudioSourceInfo {
    QString id;                         // ID único de la fuente
    SourceType type = SourceType::Unknown; // Tipo de fuente
    QString name;                       // Nombre descriptivo
    QString url;                        // URL (para fuentes de red)
    QString path;                       // Ruta (para archivos)
    bool isActive = false;              // Si está activa
    bool isValid = false;               // Si es válida
    QAudioFormat format;                // Formato de audio
    QString error;                      // Último error

    // Método para verificar si la fuente es válida
    bool canProcess() const {
        return isValid && !id.isEmpty() && format.isValid();
    }
};

#endif // AUDIO_SOURCE_INFO_H
