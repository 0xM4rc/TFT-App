#include "audio_block_model.h"

AudioBlockModel::AudioBlockModel(QObject *parent)
    : QAbstractListModel(parent)
{}

// ------------------- API pública -------------------

void AudioBlockModel::replaceAll(const QVector<AudioBlock> &blocks)
{
    beginResetModel();
    m_blocks = blocks;
    endResetModel();
}

void AudioBlockModel::appendBlocks(const QVector<AudioBlock> &blocks)
{
    if (blocks.isEmpty())
        return;

    const int first = m_blocks.size();
    const int last  = first + blocks.size() - 1;

    beginInsertRows(QModelIndex(), first, last);
    m_blocks += blocks;
    endInsertRows();
}

void AudioBlockModel::clear()
{
    if (m_blocks.isEmpty())
        return;

    beginResetModel();
    m_blocks.clear();
    endResetModel();
}

// ------------------- overrides ---------------------

int AudioBlockModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_blocks.size();
}

QVariant AudioBlockModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const int row = index.row();
    if (row < 0 || row >= m_blocks.size())
        return {};

    const AudioBlock &ab = m_blocks.at(row);

    switch (role) {
    case BlockIndexRole:   return ab.blockIndex;
    case TimestampRole:    return qint64(ab.timestamp);
    case SizeRole:         return ab.size;
    case SampleOffsetRole: return ab.sampleOffset;
    case DataRole:         return QVariant::fromValue(ab.raw);
    default:               return {};
    }
}

QHash<int, QByteArray> AudioBlockModel::roleNames() const
{
    return {
        { BlockIndexRole,   "blockIndex"   },
        { TimestampRole,    "timestamp"    },
        { SizeRole,         "size"         },
        { SampleOffsetRole, "sampleOffset" },
        { DataRole,         "data"         }
    };
}
