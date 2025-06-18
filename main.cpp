// #include <QApplication>
// #include "include/gui/rt_mainwindow.h"

// int main(int argc, char *argv[])
// {
//     QApplication app(argc, argv);
//     RTMainWindow w;
//     w.setWindowTitle("RT Audio Test");
//     w.resize(800, 400);
//     w.show();
//     return app.exec();
// }



#include <QCoreApplication>
#include <QTimer>
#include <iostream>
#include "tests/audio_tester.h"

void showMenu()
{
    std::cout << "\n=== AUDIO SOURCE TESTER ===" << std::endl;
    std::cout << "1. Full test (all sources + switching)" << std::endl;
    std::cout << "2. Microphone only" << std::endl;
    std::cout << "3. Network stream only" << std::endl;
    std::cout << "4. Source switching test" << std::endl;
    std::cout << "5. Exit" << std::endl;
    std::cout << "Enter your choice (1-5): ";
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Verificar argumentos de línea de comandos
    if (argc > 1) {
        QString arg1 = argv[1];

        AudioTester tester;

        // Configurar URL personalizada si se proporciona
        if (argc > 2) {
            tester.setNetworkUrl(argv[2]);
            std::cout << "Using custom URL: " << argv[2] << std::endl;
        }

        // Configurar duración personalizada si se proporciona
        if (argc > 3) {
            bool ok;
            int duration = QString(argv[3]).toInt(&ok);
            if (ok && duration > 0) {
                tester.setTestDuration(duration);
                std::cout << "Using custom duration: " << duration << " seconds" << std::endl;
            }
        }

        // Conectar señales
        QObject::connect(&tester, &AudioTester::testCompleted, [&]() {
            std::cout << "\n🎉 Test completed successfully!" << std::endl;
            QTimer::singleShot(1000, &app, &QCoreApplication::quit);
        });

        QObject::connect(&tester, &AudioTester::testFailed, [&](const QString& reason) {
            std::cout << "\n💥 Test failed: " << reason.toStdString() << std::endl;
            QTimer::singleShot(1000, &app, &QCoreApplication::quit);
        });

        // Ejecutar test según argumento
        if (arg1 == "full" || arg1 == "1") {
            tester.testBothSources();
        } else if (arg1 == "mic" || arg1 == "2") {
            tester.testMicrophoneOnly();
        } else if (arg1 == "net" || arg1 == "3") {
            tester.testNetworkOnly();
        } else if (arg1 == "switch" || arg1 == "4") {
            tester.testSourceSwitching();
        } else {
            std::cout << "Usage: " << argv[0] << " [full|mic|net|switch] [url] [duration_seconds]" << std::endl;
            std::cout << "Examples:" << std::endl;
            std::cout << "  " << argv[0] << " full" << std::endl;
            std::cout << "  " << argv[0] << " mic" << std::endl;
            std::cout << "  " << argv[0] << " net http://example.com/stream.mp3 10" << std::endl;
            return 1;
        }

        return app.exec();
    }

    // Modo interactivo
    AudioTester tester;

    // Conectar señales para modo interactivo
    QObject::connect(&tester, &AudioTester::testCompleted, [&]() {
        std::cout << "\n🎉 Test completed! Starting another test in 3 seconds..." << std::endl;
        QTimer::singleShot(3000, []() {
            showMenu();
        });
    });

    QObject::connect(&tester, &AudioTester::testFailed, [&](const QString& reason) {
        std::cout << "\n💥 Test failed: " << reason.toStdString() << std::endl;
        std::cout << "Restarting menu in 3 seconds..." << std::endl;
        QTimer::singleShot(3000, []() {
            showMenu();
        });
    });

    // Mostrar menú inicial
    showMenu();

    // Simular entrada de usuario (en un caso real usarías std::cin)
    // Por simplicidad, ejecutamos el test completo después de 2 segundos
    QTimer::singleShot(2000, [&]() {
        std::cout << "Auto-starting full test..." << std::endl;
        tester.startTest();
    });

    return app.exec();
}

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
