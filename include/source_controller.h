#include <QObject>
#include <QHash>
#include <QString>
#include <QAudioFormat>
#include "interfaces/audio_source.h"

class SourceController : public QObject
{
    Q_OBJECT

public:
    explicit SourceController(QObject* parent = nullptr);
    ~SourceController();

    // Gestión de fuentes
    void addSource(const QString& key, AudioSource* source);
    void removeSource(const QString& key);
    bool setActiveSource(const QString& key);

    // Control básico
    void start();
    void stop();

    // Consultas de estado
    QString activeSourceKey() const { return m_activeKey; }
    AudioSource* activeSource() const { return m_sources.value(m_activeKey, nullptr); }
    QStringList availableSources() const { return m_sources.keys(); }
    bool hasActiveSource() const { return !m_activeKey.isEmpty() && m_sources.contains(m_activeKey); }

    // Información de la fuente activa
    QAudioFormat activeFormat() const;
    bool isActiveSourceRunning() const;

signals:
    // Señales principales (mantener compatibilidad)
    void dataReady(SourceType type, const QString& id, const QByteArray& data);
    void stateChanged(SourceType type, const QString& id, bool active);
    void error(SourceType type, const QString& id, const QString& message);
    void formatDetected(SourceType type, const QString& id, const QAudioFormat& format);

    // Señales adicionales del controller
    void activeSourceChanged(SourceType type, const QString& id);
    void sourceAdded(const QString& key);
    void sourceRemoved(const QString& key);

private slots:
    void onSourceDataReady();
    void onSourceStateChanged(SourceType type, const QString& id, bool active);
    void onSourceError(SourceType type, const QString& id, const QString& message);
    void onSourceFormatDetected(SourceType type, const QString& id, const QAudioFormat& format);

private:
    void connectCurrent();
    void disconnectCurrent();

    QHash<QString, AudioSource*> m_sources;
    QString m_activeKey;
};
