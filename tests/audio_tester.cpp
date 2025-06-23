// main.cpp code

// #include <QCoreApplication>
// #include <QTimer>
// #include <iostream>
// #include "tests/audio_tester.h"

// void showMenu()
// {
//     std::cout << "\n=== AUDIO SOURCE TESTER ===" << std::endl;
//     std::cout << "1. Full test (all sources + switching)" << std::endl;
//     std::cout << "2. Microphone only" << std::endl;
//     std::cout << "3. Network stream only" << std::endl;
//     std::cout << "4. Source switching test" << std::endl;
//     std::cout << "5. Exit" << std::endl;
//     std::cout << "Enter your choice (1-5): ";
// }

// int main(int argc, char *argv[])
// {
//     QCoreApplication app(argc, argv);

//     // Verificar argumentos de línea de comandos
//     if (argc > 1) {
//         QString arg1 = argv[1];

//         AudioTester tester;

//         // Configurar URL personalizada si se proporciona
//         if (argc > 2) {
//             tester.setNetworkUrl(argv[2]);
//             std::cout << "Using custom URL: " << argv[2] << std::endl;
//         }

//         // Configurar duración personalizada si se proporciona
//         if (argc > 3) {
//             bool ok;
//             int duration = QString(argv[3]).toInt(&ok);
//             if (ok && duration > 0) {
//                 tester.setTestDuration(duration);
//                 std::cout << "Using custom duration: " << duration << " seconds" << std::endl;
//             }
//         }

//         // Conectar señales
//         QObject::connect(&tester, &AudioTester::testCompleted, [&]() {
//             std::cout << "\n🎉 Test completed successfully!" << std::endl;
//             QTimer::singleShot(1000, &app, &QCoreApplication::quit);
//         });

//         QObject::connect(&tester, &AudioTester::testFailed, [&](const QString& reason) {
//             std::cout << "\n💥 Test failed: " << reason.toStdString() << std::endl;
//             QTimer::singleShot(1000, &app, &QCoreApplication::quit);
//         });

//         // Ejecutar test según argumento
//         if (arg1 == "full" || arg1 == "1") {
//             tester.testBothSources();
//         } else if (arg1 == "mic" || arg1 == "2") {
//             tester.testMicrophoneOnly();
//         } else if (arg1 == "net" || arg1 == "3") {
//             tester.testNetworkOnly();
//         } else if (arg1 == "switch" || arg1 == "4") {
//             tester.testSourceSwitching();
//         } else {
//             std::cout << "Usage: " << argv[0] << " [full|mic|net|switch] [url] [duration_seconds]" << std::endl;
//             std::cout << "Examples:" << std::endl;
//             std::cout << "  " << argv[0] << " full" << std::endl;
//             std::cout << "  " << argv[0] << " mic" << std::endl;
//             std::cout << "  " << argv[0] << " net http://example.com/stream.mp3 10" << std::endl;
//             return 1;
//         }

//         return app.exec();
//     }

//     // Modo interactivo
//     AudioTester tester;

//     // Conectar señales para modo interactivo
//     QObject::connect(&tester, &AudioTester::testCompleted, [&]() {
//         std::cout << "\n🎉 Test completed! Starting another test in 3 seconds..." << std::endl;
//         QTimer::singleShot(3000, []() {
//             showMenu();
//         });
//     });

//     QObject::connect(&tester, &AudioTester::testFailed, [&](const QString& reason) {
//         std::cout << "\n💥 Test failed: " << reason.toStdString() << std::endl;
//         std::cout << "Restarting menu in 3 seconds..." << std::endl;
//         QTimer::singleShot(3000, []() {
//             showMenu();
//         });
//     });

//     // Mostrar menú inicial
//     showMenu();

//     // Simular entrada de usuario (en un caso real usarías std::cin)
//     // Por simplicidad, ejecutamos el test completo después de 2 segundos
//     QTimer::singleShot(2000, [&]() {
//         std::cout << "Auto-starting full test..." << std::endl;
//         tester.startTest();
//     });

//     return app.exec();
// }

// Versión con entrada real del usuario (opcional)
/*
#include <QTextStream>
#include <QSocketNotifier>

class MenuHandler : public QObject
{
    Q_OBJECT
public:
    MenuHandler(AudioTester* tester) : m_tester(tester) {
        m_notifier = new QSocketNotifier(fileno(stdin), QSocketNotifier::Read, this);
        connect(m_notifier, &QSocketNotifier::activated, this, &MenuHandler::readInput);
        showMenu();
    }

private slots:
    void readInput() {
        QTextStream stream(stdin);
        QString input = stream.readLine();

        if (input == "1") {
            m_tester->startTest();
        } else if (input == "2") {
            m_tester->testMicrophoneOnly();
        } else if (input == "3") {
            m_tester->testNetworkOnly();
        } else if (input == "4") {
            m_tester->testSourceSwitching();
        } else if (input == "5") {
            QCoreApplication::quit();
        } else {
            std::cout << "Invalid choice. Please try again." << std::endl;
            showMenu();
        }
    }

private:
    AudioTester* m_tester;
    QSocketNotifier* m_notifier;
};

#include "main.moc"
*/



#include "tests/audio_tester.h"
#include "include/source_controller.h"
#include "include/network_source.h"
#include "include/microphone_source.h"

#include <QDebug>
#include <QUrl>
#include <QCoreApplication>
#include <QMediaDevices>
#include <iostream>

AudioTester::AudioTester(QObject *parent)
    : QObject(parent)
    , m_controller(nullptr)
    , m_sequenceTimer(std::make_unique<QTimer>(this))
    , m_timeoutTimer(std::make_unique<QTimer>(this))
    , m_networkUrl("http://icecast.radiofrance.fr/fip-hifi.aac")
    , m_testDuration(30) // 30 segundos por defecto
    , m_currentStep(0)
    , m_testRunning(false)
{
    setupController();
    setupConnections();

    // Configurar timers
    m_sequenceTimer->setSingleShot(true);
    connect(m_sequenceTimer.get(), &QTimer::timeout, this, &AudioTester::executeTestSequence);

    m_timeoutTimer->setSingleShot(true);
    connect(m_timeoutTimer.get(), &QTimer::timeout, this, &AudioTester::handleTestTimeout);
}

AudioTester::~AudioTester()
{
    stopTest();
}

void AudioTester::setupController()
{
    m_controller = std::make_unique<SourceController>(this);
}

void AudioTester::setupConnections()
{
    if (!m_controller) return;

    // Conectar señales del controller
    connect(m_controller.get(), &SourceController::dataReady,
            this, &AudioTester::onDataReady);
    connect(m_controller.get(), &SourceController::stateChanged,
            this, &AudioTester::onStateChanged);
    connect(m_controller.get(), &SourceController::error,
            this, &AudioTester::onError);
    connect(m_controller.get(), &SourceController::formatDetected,
            this, &AudioTester::onFormatDetected);
    connect(m_controller.get(), &SourceController::sourceAdded,
            this, &AudioTester::onSourceAdded);
    connect(m_controller.get(), &SourceController::sourceRemoved,
            this, &AudioTester::onSourceRemoved);
    connect(m_controller.get(), &SourceController::activeSourceChanged,
            this, &AudioTester::onActiveSourceChanged);
}

void AudioTester::setupSources()
{
    std::cout << "📡 Setting up audio sources..." << std::endl;

    // Crear fuente de micrófono usando el dispositivo por defecto
    QAudioDevice defaultMic = QMediaDevices::defaultAudioInput();
    if (defaultMic.isNull()) {
        std::cout << "⚠️  No default microphone found, using null device" << std::endl;
    }

    bool micAdded = m_controller->addMicrophoneSource("microphone", defaultMic);
    if (!micAdded) {
        std::cout << "❌ Failed to add microphone source" << std::endl;
    }

    // Crear fuente de red
    QString netAdded = m_controller->addNetworkSource(
        QUrl(m_networkUrl));
    // if (!netAdded) {
    //     std::cout << "❌ Failed to add network source" << std::endl;
    // }

    // if (micAdded && netAdded) {
    //     std::cout << "✅ Sources setup completed!" << std::endl;
    // } else {
    //     std::cout << "⚠️  Sources setup completed with errors!" << std::endl;
    // }
}

void AudioTester::startTest()
{
    if (m_testRunning) {
        std::cout << "⚠️  Test already running!" << std::endl;
        return;
    }

    std::cout << "\n🚀 === AUDIO SOURCE CONTROLLER TEST STARTED ===" << std::endl;
    std::cout << "Network URL: " << m_networkUrl.toStdString() << std::endl;
    std::cout << "Test duration per source: " << m_testDuration << " seconds\n" << std::endl;

    // Reset stats
    m_stats = {};
    m_currentStep = 0;
    m_testRunning = true;

    // Setup sources
    setupSources();

    // Start test sequence
    m_sequenceTimer->start(1000); // Comenzar en 1 segundo
    m_timeoutTimer->start(120000); // Timeout total de 2 minutos
}

void AudioTester::stopTest()
{
    if (!m_testRunning) return;

    m_testRunning = false;
    m_sequenceTimer->stop();
    m_timeoutTimer->stop();

    if (m_controller) {
        m_controller->stop();
    }

    std::cout << "\n🛑 Test stopped" << std::endl;
}

void AudioTester::setNetworkUrl(const QString& url)
{
    if (m_testRunning) {
        std::cout << "⚠️  Cannot change URL while test is running" << std::endl;
        return;
    }
    m_networkUrl = url;
}

void AudioTester::setTestDuration(int seconds)
{
    if (seconds > 0 && seconds <= 300) { // Max 5 minutos por test
        m_testDuration = seconds;
    }
}

void AudioTester::testMicrophoneOnly()
{
    std::cout << "\n🎤 === MICROPHONE ONLY TEST ===" << std::endl;
    setupSources();
    m_controller->setActiveSource("microphone");
    m_controller->start();

    QTimer::singleShot(m_testDuration * 1000, [this]() {
        m_controller->stop();
        printStats();
        emit testCompleted();
    });
}

void AudioTester::testNetworkOnly()
{
    std::cout << "\n🌐 === NETWORK ONLY TEST ===" << std::endl;
    setupSources();
    m_controller->setActiveSource("network_stream");
    m_controller->start();

    QTimer::singleShot(m_testDuration * 1000, [this]() {
        m_controller->stop();
        printStats();
        emit testCompleted();
    });
}

void AudioTester::testBothSources()
{
    std::cout << "\n🔄 === BOTH SOURCES TEST ===" << std::endl;
    setupSources();

    // Test switching between sources
    m_currentStep = 0;
    executeTestSequence();
}

void AudioTester::testSourceSwitching()
{
    testBothSources(); // Same as both sources test
}

void AudioTester::executeTestSequence()
{
    if (!m_testRunning) return;

    m_currentStep++;

    switch(m_currentStep) {
    case 1:
        std::cout << "\n📍 STEP 1: Testing Microphone (" << m_testDuration << "s)" << std::endl;
        if (!m_controller->setActiveSource("microphone")) {
            std::cout << "❌ Failed to set microphone as active source" << std::endl;
            finishTest();
            return;
        }
        m_controller->start();
        m_sequenceTimer->start(m_testDuration * 1000);
        break;

    case 2:
        std::cout << "\n📍 STEP 2: Switching to Network Stream (" << m_testDuration << "s)" << std::endl;
        if (!m_controller->setActiveSource("network_stream")) {
            std::cout << "❌ Failed to set network stream as active source" << std::endl;
            finishTest();
            return;
        }
        m_controller->start();
        m_sequenceTimer->start(m_testDuration * 1000);
        break;

    case 3:
        std::cout << "\n📍 STEP 3: Quick switch back to Microphone (5s)" << std::endl;
        if (!m_controller->setActiveSource("microphone")) {
            std::cout << "❌ Failed to switch back to microphone" << std::endl;
            finishTest();
            return;
        }
        m_controller->start();
        m_sequenceTimer->start(5000);
        break;

    case 4:
        std::cout << "\n📍 STEP 4: Final Network test (5s)" << std::endl;
        if (!m_controller->setActiveSource("network_stream")) {
            std::cout << "❌ Failed to switch to network for final test" << std::endl;
            finishTest();
            return;
        }
        m_controller->start();
        m_sequenceTimer->start(5000);
        break;

    case 5:
        std::cout << "\n📍 STEP 5: Testing source removal" << std::endl;
        m_controller->stop();
        m_controller->removeSource("network_stream");
        m_sequenceTimer->start(2000);
        break;

    default:
        finishTest();
        break;
    }
}

void AudioTester::finishTest()
{
    m_testRunning = false;
    m_timeoutTimer->stop();

    std::cout << "\n✅ === TEST COMPLETED ===" << std::endl;
    printStats();
    printSourcesInfo();

    emit testCompleted();
}

void AudioTester::handleTestTimeout()
{
    std::cout << "\n⏰ Test timeout reached!" << std::endl;
    m_testRunning = false;
    emit testFailed("Test timeout");
}

void AudioTester::nextTestStep()
{
    if (m_testRunning) {
        m_sequenceTimer->start(100); // Continue in 100ms
    }
}

// Slots para eventos del controller
void AudioTester::onDataReady(SourceType type, const QString& id, const QByteArray& data)
{
    if (type == SourceType::Network) {
        m_stats.networkPackets++;
    } else {
        m_stats.microphonePackets++;
    }

    m_stats.totalBytes += data.size();

    // Mostrar progreso cada 50 paquetes
    int totalPackets = m_stats.networkPackets + m_stats.microphonePackets;
    if (totalPackets % 50 == 0) {
        std::cout << "📊 [" << totalPackets << "] "
                  << formatTypeToString(type).toStdString()
                  << " (" << id.left(8).toStdString() << "...): "
                  << data.size() << " bytes (Total: "
                  << (m_stats.totalBytes / 1024) << " KB)" << std::endl;
    }
}

void AudioTester::onStateChanged(SourceType type, const QString& id, bool active)
{
    QString state = active ? "🟢 ACTIVE" : "🔴 INACTIVE";
    std::cout << "🔄 " << formatTypeToString(type).toStdString()
              << " (" << id.left(8).toStdString() << "...) is now "
              << state.toStdString() << std::endl;
}

void AudioTester::onError(SourceType type, const QString& id, const QString& message)
{
    m_stats.errors++;
    std::cout << "❌ ERROR [" << formatTypeToString(type).toStdString() << "]: "
              << message.toStdString() << std::endl;
}

void AudioTester::onFormatDetected(SourceType type, const QString& id, const QAudioFormat& format)
{
    std::cout << "🎵 FORMAT DETECTED [" << formatTypeToString(type).toStdString() << "]: "
              << format.sampleRate() << "Hz, "
              << format.channelCount() << " channels, "
              << "Format: " << static_cast<int>(format.sampleFormat()) << std::endl;

    if (type == SourceType::Network) {
        m_stats.netFormat = format;
        m_stats.netFormatDetected = true;
    } else {
        m_stats.micFormat = format;
        m_stats.micFormatDetected = true;
    }
}

void AudioTester::onSourceAdded(const QString& key)
{
    std::cout << "➕ Source added: " << key.toStdString() << std::endl;
}

void AudioTester::onSourceRemoved(const QString& key)
{
    std::cout << "➖ Source removed: " << key.toStdString() << std::endl;
}

void AudioTester::onActiveSourceChanged(SourceType type, const QString& id)
{
    std::cout << "🔀 Active source changed to: " << formatTypeToString(type).toStdString()
        << " (" << id.left(8).toStdString() << "...)" << std::endl;
}

// Utility methods
QString AudioTester::formatTypeToString(SourceType type)
{
    return (type == SourceType::Network) ? "Network" : "Microphone";
}

void AudioTester::printSourcesInfo()
{
    std::cout << "\n📋 === SOURCES STATUS ===" << std::endl;
    if (m_controller->hasActiveSource()) {
        std::cout << "Active source: " << m_controller->activeSourceKey().toStdString() << std::endl;
        std::cout << "Active source type: " << formatTypeToString(m_controller->activeSourceType()).toStdString() << std::endl;
        std::cout << "Active source running: " << (m_controller->isActiveSourceRunning() ? "Yes" : "No") << std::endl;
    } else {
        std::cout << "No active source" << std::endl;
    }

    // Mostrar fuentes disponibles
    QStringList sources = m_controller->availableSources();
    std::cout << "Available sources (" << sources.size() << "): ";
    for (const QString& source : sources) {
        std::cout << source.toStdString() << " ";
    }
    std::cout << std::endl;
}

void AudioTester::printStats()
{
    std::cout << "\n📊 === TEST STATISTICS ===" << std::endl;
    std::cout << "Microphone packets: " << m_stats.microphonePackets << std::endl;
    std::cout << "Network packets: " << m_stats.networkPackets << std::endl;
    std::cout << "Total bytes: " << (m_stats.totalBytes / 1024) << " KB" << std::endl;
    std::cout << "Errors: " << m_stats.errors << std::endl;

    if (m_stats.micFormatDetected) {
        std::cout << "Mic format: " << m_stats.micFormat.sampleRate() << "Hz, "
                  << m_stats.micFormat.channelCount() << "ch" << std::endl;
    }

    if (m_stats.netFormatDetected) {
        std::cout << "Net format: " << m_stats.netFormat.sampleRate() << "Hz, "
                  << m_stats.netFormat.channelCount() << "ch" << std::endl;
    }

    // Mostrar formato de la fuente activa si existe
    if (m_controller->hasActiveSource()) {
        QAudioFormat activeFormat = m_controller->activeFormat();
        if (activeFormat.isValid()) {
            std::cout << "Active format: " << activeFormat.sampleRate() << "Hz, "
                      << activeFormat.channelCount() << "ch" << std::endl;
        }
    }

    std::cout << std::endl;
}
