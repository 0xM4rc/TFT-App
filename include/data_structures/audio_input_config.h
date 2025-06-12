#ifndef AUDIO_INPUT_CONFIG_H
#define AUDIO_INPUT_CONFIG_H

#include <QtGlobal>
#include <QString>

#include <QString>

struct AudioInputConfig {
    int sampleRate = 44100;
    int channels = 1;
    qint64 totalFrames = 0;
    QString filePath;

    bool normalize = false;  // opcional
};
#endif // AUDIO_INPUT_CONFIG_H
