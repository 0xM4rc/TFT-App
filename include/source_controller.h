#ifndef SOURCE_CONTROLLER_H
#define SOURCE_CONTROLLER_H

#include <QObject>
#include <QHash>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QUrl>
#include "include/interfaces/audio_source.h"

class NetworkSource;
class MicrophoneSource;

class SourceController : public QObject
{
    Q_OBJECT

public:
    explicit SourceController(QObject* parent = nullptr);
    ~SourceController();

    // Métodos para agregar fuentes (thread-safe)
    QString addNetworkSource(const QUrl& url);
    bool addMicrophoneSource(const QString& key, const QAudioDevice& device = QAudioDevice());

    // Gestión de fuentes
    void removeSource(const QString& key);
    bool setActiveSource(const QString& key);

    // Control de reproducción
    void start();
    void stop();

    // Información de estado
    QAudioFormat activeFormat() const;
    bool isActiveSourceRunning() const;
    bool hasActiveSource() const { return !m_activeKey.isEmpty(); }

    // Información adicional
    QStringList availableSources() const;
    QString activeSourceKey() const;
    SourceType activeSourceType() const;

    bool updateNetworkSource(const QString& key, const QUrl& newUrl);
    bool updateMicrophoneSource(const QString& key, const QAudioDevice& newDevice);

public slots:
    // Métodos para control masivo
    void stopAllSources();
    void clearAllSources();

signals:
    // Señales de gestión de fuentes
    void sourceAdded(const QString& key);
    void sourceRemoved(const QString& key);
    void activeSourceChanged(SourceType type, const QString& id);

    // Señales de datos y estado (desde fuentes activas)
    void dataReady(SourceType type, const QString& id, const QByteArray& data);
    void stateChanged(SourceType type, const QString& id, bool active);
    void error(SourceType type, const QString& id, const QString& message);
    void formatDetected(SourceType type, const QString& id, const QAudioFormat& format);

    void sourceUpdated(const QString& key);

private slots:
    // Manejadores de señales de fuentes
    void onSourceDataReady();
    void onSourceStateChanged(SourceType type, const QString& id, bool active);
    void onSourceError(SourceType type, const QString& id, const QString& message);
    void onSourceFormatDetected(SourceType type, const QString& id, const QAudioFormat& format);

private:
    // Métodos internos
    void connectCurrent();
    void disconnectCurrent();

    // Datos miembro
    QHash<QString, AudioSource*> m_sources;
    QString m_activeKey;
};

#endif // SOURCE_CONTROLLER_H
