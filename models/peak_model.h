#ifndef PEAK_MODEL_H
#define PEAK_MODEL_H

#include <QAbstractListModel>
#include <QVector>
#include "core/audio_db.h"

/**
 * @brief Modelo para exponer registros de picos (min/max) de audio
 *        tanto en tiempo real como históricos desde la base de datos.
 */
class PeakModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize NOTIFY maxSizeChanged)

public:
    enum Roles {
        TimestampRole    = Qt::UserRole + 1,  // tiempo en ns
        BlockIndexRole,                      // índice de bloque
        SampleOffsetRole,                    // offset de muestra
        MinRole,                             // valor mínimo
        MaxRole                              // valor máximo
    };

    explicit PeakModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
        , m_db(nullptr)
        , m_maxSize(1000)
        , m_timeStart(0)
        , m_timeEnd(LLONG_MAX)
    {}

    // QAbstractListModel interface
    int rowCount(const QModelIndex& = {}) const override { return m_peaks.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_peaks.size())
            return {};
        const auto &p = m_peaks.at(index.row());
        switch (role) {
        case TimestampRole:    return qint64(p.timestamp);
        case BlockIndexRole:   return p.blockIndex;
        case SampleOffsetRole: return p.sampleOffset;
        case MinRole:          return p.minValue;
        case MaxRole:          return p.maxValue;
        default:               return {};
        }
    }
    QHash<int,QByteArray> roleNames() const override {
        return {
            {TimestampRole,    "timestamp"},
            {BlockIndexRole,   "blockIndex"},
            {SampleOffsetRole, "sampleOffset"},
            {MinRole,          "minValue"},
            {MaxRole,          "maxValue"}
        };
    }

    // Configuración DB e histórico
    void setDatabase(AudioDb* db) { m_db = db; }
    Q_INVOKABLE void setTimeRange(qint64 startNs, qint64 endNs) {
        m_timeStart = startNs;
        m_timeEnd   = endNs;
    }
    Q_INVOKABLE void refreshHistory() {
        if (!m_db) return;
        beginResetModel();
        m_peaks.clear();
        auto list = m_db->getPeaksByTime(m_timeStart, m_timeEnd);
        // recortar si excede maxSize
        int count = list.size();
        int keep = qMin(count, m_maxSize);
        for (int i = count - keep; i < count; ++i)
            m_peaks.append(list.at(i));
        endResetModel();
    }

    // Control de tamaño de la ventana
    int maxSize() const { return m_maxSize; }
    void setMaxSize(int s) {
        if (s == m_maxSize) return;
        m_maxSize = s;
        trimIfNeeded();
        emit maxSizeChanged(s);
    }

public slots:
    void appendPeak(const PeakRecord &rec) {
        const int oldCount = m_peaks.size();
        beginInsertRows({}, oldCount, oldCount);
        m_peaks.append(rec);
        endInsertRows();
        trimIfNeeded();
    }

    void clear() {
        beginResetModel();
        m_peaks.clear();
        endResetModel();
    }

signals:
    void maxSizeChanged(int);

private:
    void trimIfNeeded() {
        if (m_peaks.size() <= m_maxSize) return;
        int over = m_peaks.size() - m_maxSize;
        beginRemoveRows({}, 0, over - 1);
        m_peaks.erase(m_peaks.begin(), m_peaks.begin() + over);
        endRemoveRows();
    }

    AudioDb*              m_db;
    int                   m_maxSize;
    qint64                m_timeStart;
    qint64                m_timeEnd;
    QVector<PeakRecord>   m_peaks;
};

#endif // PEAK_MODEL_H
