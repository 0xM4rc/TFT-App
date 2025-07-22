#ifndef AUDIO_BLOCK_MODEL_H
#define AUDIO_BLOCK_MODEL_H

#include <QAbstractListModel>
#include <QVector>
#include <QByteArray>
#include <QHash>

/*!
 * \brief Estructura con la información básica de un bloque de audio.
 */
struct AudioBlock
{
    qint64  blockIndex;    //!< Índice (posición) del bloque dentro de la grabación
    quint64 timestamp;     //!< Marca de tiempo en nanosegundos
    qint64  size;          //!< Tamaño en bytes
    qint64  sampleOffset;  //!< Desplazamiento de muestra dentro de la grabación
    QByteArray raw;        //!< (opcional) Datos de audio crudos
};

/*!
 * \brief Modelo de solo-lectura que expone una secuencia de AudioBlock.
 *
 *  – No realiza acceso a BD ni E/S: todo lo recibe a través de sus métodos
 *    públicos (`replaceAll`, `appendBlocks`, `clear`).
 *  – Ideal para tests y para desacoplar la capa de datos de la UI.
 */
class AudioBlockModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        BlockIndexRole = Qt::UserRole + 1,
        TimestampRole,
        SizeRole,
        SampleOffsetRole,
        DataRole
    };
    Q_ENUM(Roles)

    explicit AudioBlockModel(QObject *parent = nullptr);

    // -------- API para el “loader” externo --------
    void replaceAll(const QVector<AudioBlock> &blocks);
    void appendBlocks(const QVector<AudioBlock> &blocks);
    void clear();

    // -------- QAbstractItemModel overrides --------
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    QVector<AudioBlock> m_blocks;
};

#endif // AUDIO_BLOCK_MODEL_H
