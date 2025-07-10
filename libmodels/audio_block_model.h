#ifndef AUDIO_BLOCK_MODEL_H
#define AUDIO_BLOCK_MODEL_H

#include "audio_db.h"
#include <QAbstractListModel>
#include <QVector>
#include <QHash>
#include <climits>

// ----------------------------------------------------------------------------
// Estructo para describir un bloque de audio con metadatos
// ----------------------------------------------------------------------------
struct AudioBlock {
    qint64  blockIndex;    // Índice del bloque en la base de datos
    quint64 timestamp;     // Timestamp en nanosegundos
    qint64  size;          // Tamaño del audio en bytes
    qint64  sampleOffset;  // Offset de muestras dentro de la grabación
};

// ----------------------------------------------------------------------------
// Modelo para exponer bloques de audio en una vista de lista o tabla.
// Permite cargar rangos específicos para no cargar todo el historial.
// ----------------------------------------------------------------------------
class AudioBlockModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        BlockIndexRole = Qt::UserRole + 1,
        TimestampRole,
        SizeRole,
        SampleOffsetRole,
        DataRole
    };

    explicit AudioBlockModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
        , m_db(nullptr)
        , m_blockStart(0)
        , m_limit(100)
    {}

    // Número de filas actuales (añadidas tras el último refreshRange)
    int rowCount(const QModelIndex& = QModelIndex()) const override {
        return m_blocks.size();
    }

    // Devuelve el dato solicitado según el role
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_blocks.size())
            return {};
        const auto& ab = m_blocks.at(index.row());
        switch (role) {
        case BlockIndexRole:   return ab.blockIndex;
        case TimestampRole:    return qint64(ab.timestamp);
        case SizeRole:         return ab.size;
        case SampleOffsetRole: return ab.sampleOffset;
        case DataRole:         return QVariant::fromValue(QByteArray()); // raw data si se desea
        default:               return {};
        }
    }

    // Mapear role → nombre en QML o en delegado
    QHash<int,QByteArray> roleNames() const override {
        return {
            { BlockIndexRole,   "blockIndex"   },
            { TimestampRole,    "timestamp"    },
            { SizeRole,         "size"         },
            { SampleOffsetRole, "sampleOffset" },
            { DataRole,         "data"         }
        };
    }

    // Asignar la base de datos de audio a consultar
    void setDatabase(AudioDb* db) { m_db = db; }

    // Configurar el rango: bloque inicial y cuántos bloques cargar
    Q_INVOKABLE void setRange(qint64 startBlock, int nBlocks) {
        if (m_blockStart == startBlock && m_limit == nBlocks)
            return;
        m_blockStart = startBlock;
        m_limit      = nBlocks;
    }

    // Recargar desde la base de datos solo el rango especificado
    Q_INVOKABLE void refreshRange() {
        if (!m_db)
            return;
        beginResetModel();
        m_blocks.clear();
        // Obtener raw audio de la base de datos en ese rango
        auto rawList = m_db->getBlocksByOffset(m_blockStart, m_limit);
        for (int i = 0; i < rawList.size(); ++i) {
            AudioBlock ab;
            ab.blockIndex   = m_blockStart + i;
            ab.size         = rawList[i].size();
            ab.timestamp    = m_db->getBlockTimestamp(ab.blockIndex);
            ab.sampleOffset = m_db->getBlockSampleOffset(ab.blockIndex);
            m_blocks.append(ab);
        }
        endResetModel();
    }

private:
    AudioDb*             m_db;            // Servicio de acceso a base de datos
    qint64               m_blockStart;    // Bloque de inicio para la carga
    int                  m_limit;         // Número de bloques a cargar
    QVector<AudioBlock>  m_blocks;        // Datos en memoria
};

#endif // AUDIO_BLOCK_MODEL_H
