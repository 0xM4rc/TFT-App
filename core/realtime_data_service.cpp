#include "realtime_data_service.h"
#include <QtGlobal>
#include <limits>
#include <algorithm>

RealtimeDataService::RealtimeDataService(QObject* parent) : QObject(parent) {}

void RealtimeDataService::setModels(AudioBlockModel* blk, PeakModel* peaks, SpectrogramModel* spec) {
    m_blockModel = blk;
    m_peakModel  = peaks;
    m_specModel  = spec;
}

void RealtimeDataService::setDatabase(AudioDb* db) {
    m_db = db;
}

void RealtimeDataService::onFramesReady(const QVector<FrameData>& frames) {
    if (!m_specModel && !m_peakModel && !m_blockModel) return;

    // 1) Empujar espectrograma tal cual
    if (m_specModel && !frames.isEmpty()) {
        // Asegurar UI thread si vienes de otro hilo:
        if (thread() == m_specModel->thread()) {
            m_specModel->appendFrames(frames);
        } else {
            QMetaObject::invokeMethod(m_specModel, [this, frames](){
                m_specModel->appendFrames(frames);
            }, Qt::QueuedConnection);
        }
    }

    // 2) Derivar picos (si no te llega de BD/DSP)
    if (m_peakModel) {
        QVector<PeakRecord> derived;
        derived.reserve(frames.size());
        for (const auto& f : frames)
            derived.append(computePeakFromFrame(f));

        // inserción uno a uno (o agrupa si añades un appendBatch)
        for (const auto& p : derived) {
            if (thread() == m_peakModel->thread()) {
                m_peakModel->appendPeak(p);
            } else {
                QMetaObject::invokeMethod(m_peakModel, [this, p](){
                    m_peakModel->appendPeak(p);
                }, Qt::QueuedConnection);
            }
        }
    }

    // 3) (Opcional) también crear un “AudioBlock” de visualización
    if (m_blockModel) {
        QVector<AudioBlock> blocks;
        blocks.reserve(frames.size());
        for (int i=0; i<frames.size(); ++i) {
            const auto& f = frames[i];
            AudioBlock b;
            b.blockIndex   = -1; // si no lo conoces, o pásalo desde DSP
            b.timestamp    = f.timestamp;
            b.sampleOffset = f.sampleOffset;
            b.size         = 0;  // si no hay raw
            // b.raw       = ...  // sólo si quieres mostrar el raw
            blocks.append(b);
        }

        if (!blocks.isEmpty()) {
            if (thread() == m_blockModel->thread()) {
                m_blockModel->appendBlocks(blocks);
            } else {
                QMetaObject::invokeMethod(m_blockModel, [this, blocks](){
                    m_blockModel->appendBlocks(blocks);
                }, Qt::QueuedConnection);
            }
        }
    }
}

void RealtimeDataService::onPeakReady(const PeakRecord& rec) {
    if (!m_peakModel) return;
    if (thread() == m_peakModel->thread()) {
        m_peakModel->appendPeak(rec);
    } else {
        QMetaObject::invokeMethod(m_peakModel, [this, rec](){
            m_peakModel->appendPeak(rec);
        }, Qt::QueuedConnection);
    }
}

void RealtimeDataService::onBlockReady(const AudioBlock& block) {
    if (!m_blockModel) return;
    if (thread() == m_blockModel->thread()) {
        m_blockModel->appendBlocks({block});
    } else {
        QMetaObject::invokeMethod(m_blockModel, [this, block](){
            m_blockModel->appendBlocks({block});
        }, Qt::QueuedConnection);
    }
}

PeakRecord RealtimeDataService::computePeakFromFrame(const FrameData& f) {
    PeakRecord r{};
    r.blockIndex   = -1;                 // rellena si lo tienes
    r.sampleOffset = f.sampleOffset;
    r.timestamp    = f.timestamp;

    float mn =  std::numeric_limits<float>::infinity();
    float mx = -std::numeric_limits<float>::infinity();

    // usamos waveform (downsample) para min/max rápido
    for (float v : f.waveform) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }

    // si waveform vacío, puedes estimar a partir del espectro (peor) o ignorar
    if (f.waveform.isEmpty()) {
        mn = 0.f; mx = 0.f;
    }

    r.minValue = mn;
    r.maxValue = mx;
    return r;
}
