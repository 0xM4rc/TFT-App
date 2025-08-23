// config/audio_configs.h
#pragma once
#include "qobject.h"
#include "qurl.h"
#include <QString>
#include <QAudioFormat>

struct IReceiverConfig {
    virtual ~IReceiverConfig() = default;
};

struct PhysicalInputConfig : public IReceiverConfig {
    int  sampleRate          = 44100;
    int  channelCount        = 2;
    QAudioFormat::SampleFormat sampleFormat = QAudioFormat::Float;

    QString deviceId;
    bool    usePreferred           = false;
    bool    fallbackToPreferred    = true;
    int     bufferSize             = 4096;

    // ---- Validación ----
    int isValid() const {
        // Rangos aceptados: 8 kHz – 384 kHz
        if (sampleRate < 8000 || sampleRate > 384000)
            return 1;

        // Canales razonables (1–32)
        if (channelCount < 1 || channelCount > 32)
            return 2;

        // Formato debe ser válido (excluye Unknown)
        if (sampleFormat == QAudioFormat::Unknown)
            return 3;

        return 0; // Success
    }
};

struct NetworkInputConfig : public IReceiverConfig {
    // Fuente
    QString url;

    // Appsink / buffering
    int  maxBuffers   = 10;      // >0
    bool dropBuffers  = true;    // drop=true descarta cuando se llena
    bool syncAudio    = false;   // appsink sync (normalmente false en streaming)
    bool asyncSink    = false;   // appsink async (añadido)

    // Bus polling
    int  busTimerInterval = 100;

    // Timeouts / reconexión
    int  connectionTimeoutMs    = 30000; // souphttpsrc timeout (ms) (0 => sin timeout)
    bool autoReconnect          = false;
    int  maxReconnectAttempts   = 3;     // -1 => infinito
    int  reconnectDelayMs       = 5000;  // >= 200 recomendado

    // Audio target
    int  targetSampleRate       = 0;     // 0 => no forzar
    int  targetChannels         = 0;     // 0 => no forzar
    QAudioFormat::SampleFormat targetFormat = QAudioFormat::Unknown;
    bool enforceFormat          = false; // si true y no se puede -> error

    // Debug / logging
    bool enableDebugOutput = true;
    bool logBufferStats    = false;

    // HTTP
    QString userAgent = "NetworkReceiver/1.0";
    QStringList extraHeaders;            // "Header: Value"

    // Seguridad (opcional)
    bool allowInsecureTLS = false;       // (solo pruebas)

    // Validación estructurada
    struct ValidationResult {
        bool ok = true;
        QStringList errors;
        QStringList warnings;
        bool adjusted = false;
    };

    ValidationResult validate(bool normalize = true) {
        ValidationResult r;
        auto err = [&](const QString& e){ r.ok=false; r.errors<<e; };
        auto warn= [&](const QString& w){ r.warnings<<w; };

        // URL
        if (url.trimmed().isEmpty()) {
            err("url vacía");
        } else {
            QUrl q(url);
            if (!q.isValid()) err(QString("URL inválida: %1").arg(url));
            else {
                const QString scheme = q.scheme().toLower();
                static const QStringList supported = {
                    "http","https","file","udp","rtp","rtsp"
                };
                if (!supported.contains(scheme))
                    warn(QString("Esquema '%1' no manejado explícitamente (se usará HTTP por defecto)").arg(scheme));
            }
        }

        auto clamp = [&](int& v, int lo, int hi, const char* name){
            if (v < lo || v > hi) {
                if (normalize) {
                    int orig=v;
                    v = std::clamp(v, lo, hi);
                    r.adjusted = true;
                    warn(QString("%1 %2 ajustado a %3").arg(name).arg(orig).arg(v));
                } else {
                    err(QString("%1 fuera de rango (%2 - %3): %4").arg(name).arg(lo).arg(hi).arg(v));
                }
            }
        };

        clamp(maxBuffers, 1, 500, "maxBuffers");
        clamp(busTimerInterval, 5, 1000, "busTimerInterval");
        if (connectionTimeoutMs < 0) {
            normalize ? (connectionTimeoutMs=30000, r.adjusted=true, warn("connectionTimeoutMs negativo -> 30000"))
                      : err("connectionTimeoutMs negativo");
        }
        if (reconnectDelayMs < 0) {
            normalize ? (reconnectDelayMs=1000, r.adjusted=true, warn("reconnectDelayMs negativo -> 1000"))
                      : err("reconnectDelayMs negativo");
        }
        if (autoReconnect) {
            if (maxReconnectAttempts == 0)
                warn("autoReconnect activo pero maxReconnectAttempts=0 (no reintentos)");
            if (reconnectDelayMs < 200)
                warn("reconnectDelayMs < 200 ms puede provocar tormenta de reconexiones");
        }

        // Audio target
        if (targetSampleRate < 0) {
            err("targetSampleRate negativo");
        } else if (targetSampleRate > 0 && (targetSampleRate < 8000 || targetSampleRate > 384000)) {
            if (normalize) {
                warn(QString("Clamp targetSampleRate %1").arg(targetSampleRate));
                targetSampleRate = std::clamp(targetSampleRate, 8000, 384000);
                r.adjusted = true;
            } else {
                err(QString("targetSampleRate fuera de rango: %1").arg(targetSampleRate));
            }
        }
        if (targetChannels < 0) {
            err("targetChannels negativo");
        } else if (targetChannels > 0 && (targetChannels < 1 || targetChannels > 32)) {
            if (normalize) {
                warn(QString("Clamp targetChannels %1").arg(targetChannels));
                targetChannels = std::clamp(targetChannels, 1, 32);
                r.adjusted = true;
            } else {
                err(QString("targetChannels fuera de rango"));
            }
        }
        if (enforceFormat && targetFormat == QAudioFormat::Unknown)
            err("enforceFormat=true pero targetFormat==Unknown");

        if (allowInsecureTLS)
            warn("allowInsecureTLS=true (solo usar en desarrollo)");

        return r;
    }

    QString buildCaps() const {
        QStringList caps;
        if (targetFormat != QAudioFormat::Unknown) {
            switch (targetFormat) {
            case QAudioFormat::Int16: caps << "format=S16LE"; break;
            case QAudioFormat::Float: caps << "format=F32LE"; break;
            case QAudioFormat::Int32: caps << "format=S32LE"; break;
            default: break;
            }
        }
        if (targetSampleRate > 0) caps << QString("rate=%1").arg(targetSampleRate);
        if (targetChannels   > 0) caps << QString("channels=%1").arg(targetChannels);
        if (caps.isEmpty()) return {};
        return "audio/x-raw," + caps.join(',');
    }

    QString getPipelineString() const {
        QUrl q(url);
        const QString scheme = q.scheme().toLower();
        QString source;

        if (scheme=="http" || scheme=="https" || scheme.isEmpty()) {
            int timeoutSec = connectionTimeoutMs > 0 ? (connectionTimeoutMs+999)/1000 : 0;
            source = QString("souphttpsrc location=\"%1\" user-agent=\"%2\" %3 ")
                     .arg(url)
                     .arg(userAgent)
                     .arg(timeoutSec>0 ? QString("timeout=%1").arg(timeoutSec) : QString());
            // extraHeaders se podrían añadir vía 'headers=' (requiere formateo)
        } else if (scheme=="file") {
            source = QString("filesrc location=\"%1\" ").arg(url);
        } else if (scheme=="udp") {
            // Ejemplo: udp://host:port
            source = QString("udpsrc uri=\"%1\" ").arg(url);
        } else {
            // fallback
            source = QString("souphttpsrc location=\"%1\" user-agent=\"%2\" ").arg(url, userAgent);
        }

        QString decode = " ! decodebin name=decoder";
        QString convert = " ! audioconvert ! audioresample";

        QString capsFilter;
        QString caps = buildCaps();
        if (!caps.isEmpty())
            capsFilter = QString(" ! capsfilter caps=\"%1\"").arg(caps);

        QString sink = QString(" ! appsink name=sink emit-signals=true "
                               "sync=%1 async=%2 max-buffers=%3 drop=%4")
                        .arg(syncAudio ? "true":"false")
                        .arg(asyncSink ? "true":"false")
                        .arg(maxBuffers)
                        .arg(dropBuffers ? "true":"false");

        return source + decode + convert + capsFilter + sink;
    }
};

/**
 * @brief Configuración para el procesamiento DSP
 */
struct DSPConfig {
    int blockSize = 1024;       ///< Tamaño del bloque para procesamiento
    int fftSize = 1024;         ///< Tamaño de la FFT para espectrograma
    int sampleRate = 44100;     ///< Frecuencia de muestreo
    bool enableSpectrum = true; ///< Habilitar cálculo de espectro
    bool enablePeaks = true;    ///< Habilitar detección de picos
    int waveformSize = 512;     ///< Tamaño del waveform para picos

    // Configuración específica para espectrograma
    int hopSize = 512;          ///< Tamaño del salto entre ventanas
    int windowType = 1;         ///< Tipo de ventana (0=Rectangular, 1=Hann, etc.)
    double kaiserBeta = 8.0;    ///< Parámetro beta para ventana Kaiser
    double gaussianSigma = 0.4; ///< Parámetro sigma para ventana Gaussiana
    bool logScale = true;       ///< Aplicar escala logarítmica (dB)
    float noiseFloor = -100.0f; ///< Piso de ruido en dB

    // Constructor por defecto
    DSPConfig() = default;

    // Constructor con parámetros básicos
    DSPConfig(int blockSz, int fftSz, int sampleRt = 44100)
        : blockSize(blockSz)
        , fftSize(fftSz)
        , sampleRate(sampleRt)
        , hopSize(fftSz / 2)  // Hop size típico es la mitad del FFT size
    {}
};
