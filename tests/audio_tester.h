#ifndef AUDIOTESTER_H
#define AUDIOTESTER_H

#include <QObject>
#include <QTimer>
#include <QAudioFormat>
#include <memory>

// Forward declarations
class SourceController;
enum class SourceType;

class AudioTester : public QObject
{
    Q_OBJECT

public:
    explicit AudioTester(QObject *parent = nullptr);
    ~AudioTester();

    // Métodos públicos para controlar el test
    void startTest();
    void stopTest();
    void setNetworkUrl(const QString& url);
    void setTestDuration(int seconds);

    // Métodos para testing específico
    void testMicrophoneOnly();
    void testNetworkOnly();
    void testBothSources();
    void testSourceSwitching();

signals:
    void testCompleted();
    void testFailed(const QString& reason);

private slots:
    void onDataReady(SourceType type, const QString& id, const QByteArray& data);
    void onStateChanged(SourceType type, const QString& id, bool active);
    void onError(SourceType type, const QString& id, const QString& message);
    void onFormatDetected(SourceType type, const QString& id, const QAudioFormat& format);
    void onSourceAdded(const QString& key);
    void onSourceRemoved(const QString& key);
    void onActiveSourceChanged(SourceType type, const QString& id);

    void executeTestSequence();
    void handleTestTimeout();

private:
    void setupController();
    void setupConnections();
    void setupSources();
    void printSourcesInfo();
    void printStats();
    QString formatTypeToString(SourceType type);

    // Test sequence methods
    void nextTestStep();
    void finishTest();

private:
    std::unique_ptr<SourceController> m_controller;
    std::unique_ptr<QTimer> m_sequenceTimer;
    std::unique_ptr<QTimer> m_timeoutTimer;

    // Test configuration
    QString m_networkUrl;
    int m_testDuration;
    int m_currentStep;
    bool m_testRunning;

    // Statistics
    struct {
        int microphonePackets = 0;
        int networkPackets = 0;
        int totalBytes = 0;
        int errors = 0;
        QAudioFormat micFormat;
        QAudioFormat netFormat;
        bool micFormatDetected = false;
        bool netFormatDetected = false;
    } m_stats;
};

#endif // AUDIOTESTER_H
