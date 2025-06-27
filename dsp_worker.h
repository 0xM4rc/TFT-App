#ifndef DSP_WORKER_H
#define DSP_WORKER_H

#include <QObject>
#include <QVector>
#include <QTimer>
#include "audio_db.h"

struct DSPConfig {
    int sampleRate    = 48000;
    int blockSize     = 2048;  // muestras por bloque de waveform
    int fftSize       = 1024;  // ventana FFT
    int hopSize       = 512;   // salto entre ventanas FFT
};

class DSPWorker : public QObject {
    Q_OBJECT

public:
    explicit DSPWorker(const DSPConfig& cfg, AudioDb* db, QObject* parent = nullptr);
    ~DSPWorker() override;

public slots:
    /// Conectar a chunkReady(buffer, timestamp)
    void processChunk(const QByteArray& pcmBytes, qint64 timestamp);

signals:
    /// Para waveform: min/max de cada bloque
    void minMaxReady(float minVal, float maxVal, qint64 blockTimestamp);
    /// Para espectrograma: columna FFT
    void specColumnReady(const QVector<float>& column, qint64 columnTimestamp);

private slots:
    /// Puedes usar este timer para tareas periódicas (opcional)
    void onFlushTimer();

private:
    DSPConfig      m_cfg;
    AudioDb*       m_db;             // puntero no-owning a tu AudioDb
    qint64         m_totalSamples    = 0;  // offset global en muestras
    int            m_blockIndex      = 0;

    QVector<float> m_accumBuffer;   // acumulador de muestras float
    QTimer         m_flushTimer;    // opcional, si quieres periodicidad

    void handleBlock(const QVector<float>& blockSamples, qint64 timestamp);
    QVector<float> bytesToFloats(const QByteArray& pcm) const;
};

#endif // DSP_WORKER_H
