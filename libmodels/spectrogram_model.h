#ifndef SPECTROGRAM_MODEL_H
#define SPECTROGRAM_MODEL_H

#include <QAbstractListModel>
#include <QVector>
#include <QtGlobal>
#include "dsp_worker.h"  // define FrameData

/**
 * @brief Modelo para exponer frames de espectrograma generados por DSPWorker
 *        tanto en tiempo real como (opcional) históricos.
 */
class SpectrogramModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int maxSize READ maxSize WRITE setMaxSize NOTIFY maxSizeChanged)

public:
    enum Roles {
        TimestampRole     = Qt::UserRole + 1,  // tiempo en ns
        BlockIndexRole,                       // índice de bloque
        MagnitudesRole,                       // vector de magnitudes
        FrequenciesRole,                      // vector de frecuencias
        WindowGainRole                        // ganancia de ventana aplicada
    };

    explicit SpectrogramModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
        , m_maxSize(500)
        , m_timeStart(0)
        , m_timeEnd(LLONG_MAX)
    {}

    // QAbstractListModel interface
    int rowCount(const QModelIndex& = {}) const override {
        return m_frames.size();
    }

    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_frames.size())
            return {};
        const FrameData &f = m_frames.at(index.row());
        switch(role) {
        case TimestampRole:    return qint64(f.timestamp);
        case BlockIndexRole:   return f.sampleOffset;  // or f.sampleOffset?
        case MagnitudesRole:   return QVariant::fromValue(f.spectrum);
        case FrequenciesRole:  return QVariant::fromValue(f.frequencies);
        case WindowGainRole:   return f.windowGain;
        default:               return {};
        }
    }

    QHash<int,QByteArray> roleNames() const override {
        return {
            {TimestampRole,    "timestamp"},
            {BlockIndexRole,   "blockIndex"},
            {MagnitudesRole,   "magnitudes"},
            {FrequenciesRole,  "frequencies"},
            {WindowGainRole,   "windowGain"}
        };
    }

    // Control de tamaño de ventana
    int maxSize() const { return m_maxSize; }
    void setMaxSize(int s) {
        if (s == m_maxSize) return;
        m_maxSize = s;
        trimIfNeeded();
        emit maxSizeChanged(s);
    }

public slots:
    /**
     * @brief Inserta nuevos frames de espectrograma en tiempo real
     */
    void appendFrames(const QVector<FrameData> &frames) {
        if (frames.isEmpty()) return;
        int old = m_frames.size();
        int cnt = frames.size();
        beginInsertRows({}, old, old + cnt - 1);
        for (const auto &f : frames)
            m_frames.append(f);
        endInsertRows();
        trimIfNeeded();
    }

    /**
     * @brief Limpia todos los frames almacenados
     */
    void clear() {
        beginResetModel();
        m_frames.clear();
        endResetModel();
    }

    /**
     * @brief (Opcional) define rango temporal para historial
     */
    Q_INVOKABLE void setTimeRange(qint64 startNs, qint64 endNs) {
        m_timeStart = startNs;
        m_timeEnd   = endNs;
    }

    /**
     * @brief (Opcional) refresca frames históricos desde fuente externa
     */
    Q_INVOKABLE void refreshHistory() {
        // Implementar si se dispone de fuente histórica (AudioDb o similar)
        Q_UNUSED(m_timeStart)
        Q_UNUSED(m_timeEnd)
    }

signals:
    void maxSizeChanged(int);

private:
    void trimIfNeeded() {
        if (m_frames.size() <= m_maxSize) return;
        int over = m_frames.size() - m_maxSize;
        beginRemoveRows({}, 0, over - 1);
        m_frames.erase(m_frames.begin(), m_frames.begin() + over);
        endRemoveRows();
    }

    int                 m_maxSize;
    qint64              m_timeStart;
    qint64              m_timeEnd;
    QVector<FrameData>  m_frames;
};

#endif // SPECTROGRAM_MODEL_H
