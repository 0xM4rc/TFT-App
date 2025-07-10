#ifndef MODELS_H
#define MODELS_H

#include <QAbstractListModel>
#include <QVector>
#include "audio_db.h"
#include "dsp_worker.h"

// Model to expose audio blocks stored in the database
class AudioBlockModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { BlockIndexRole = Qt::UserRole + 1, TimestampRole, SizeRole, DataRole };
    explicit AudioBlockModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDatabase(AudioDb* db);
    void refresh();

    Q_INVOKABLE void setRange(qint64 blockStart, int nBlocks) {
        m_blockStart = blockStart;
        m_limit      = nBlocks;
    }

    Q_INVOKABLE void refreshRange() {
        beginResetModel();
        m_blocks.clear();
        // getBlocksByOffset devuelve sólo los nBlocks a partir de blockStart
        auto data = m_db->getBlocksByOffset(m_blockStart, m_limit);
        for (int i = 0; i < data.size(); ++i) {
            AudioBlock ab;
            ab.blockIndex = m_blockStart + i;
            ab.size       = data[i].size();
            ab.timestamp  = m_db->getBlockTimestamp(ab.blockIndex);
            m_blocks.append(ab);
        }
        endResetModel();
    }

private:
    qint64 m_blockStart = 0;
    int    m_limit      = 100;    // por defecto 100 bloques
    QVector<AudioBlock> m_blocks;
};


// Model to expose peak records for plotting or list view
class PeakModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TimestampRole = Qt::UserRole + 1, BlockIndexRole, OffsetRole, MinRole, MaxRole };
    explicit PeakModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDatabase(AudioDb* db);
    void refresh(qint64 tStart = 0, qint64 tEnd = LLONG_MAX);

private:
    AudioDb* m_db = nullptr;
    QVector<PeakRecord> m_peaks;
};

// Model for spectrogram frames produced by DSPWorker
class SpectrogramModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { TimestampRole = Qt::UserRole + 1, BlockIndexRole, MagnitudesRole, FrequenciesRole };
    explicit SpectrogramModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void appendFrames(const QVector<FrameData>& frames);
    void clear();

private:
    QVector<FrameData> m_frames;
};

#endif // MODELS_H

