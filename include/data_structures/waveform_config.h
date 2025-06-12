#ifndef WAVEFORM_CONFIG_H
#define WAVEFORM_CONFIG_H

struct WaveformConfig {
    int samplesPerPixel = 128;     // downsample factor
    float zoom = 1.0f;             // UI zoom
    int minHeight = -1, maxHeight = 1;  // vertical range (amplitud)
    bool drawGrid = true;
    bool fillUnderCurve = true;
};

#endif // WAVEFORM_CONFIG_H
