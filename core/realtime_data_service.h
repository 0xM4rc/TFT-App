#ifndef REALTIME_DATA_SERVICE_H
#define REALTIME_DATA_SERVICE_H

#include <QObject>
#include "core/dsp_worker.h"
#include "core/audio_db.h"
#include "models/audio_block_model.h"
#include "models/peak_model.h"
#include "models/spectrogram_model.h"

class RealtimeDataService : public QObject {
    Q_OBJECT
public:
    explicit RealtimeDataService(QObject* parent = nullptr);

    void setModels(AudioBlockModel* blk, PeakModel* peaks, SpectrogramModel* spec);
    void setDatabase(AudioDb* db);

public slots:
    void onFramesReady(const QVector<FrameData>& frames);
    void onPeakReady(const PeakRecord& rec);          // opcional si viene de BD/DSP
    void onBlockReady(const AudioBlock& block);       // opcional si viene de BD

private:
    static PeakRecord computePeakFromFrame(const FrameData& f);

    AudioBlockModel*   m_blockModel   = nullptr;
    PeakModel*         m_peakModel    = nullptr;
    SpectrogramModel*  m_specModel    = nullptr;
    AudioDb*           m_db           = nullptr;
};

#endif // REALTIME_DATA_SERVICE_H
