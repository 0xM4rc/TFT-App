#include <QApplication>
#include <QStyleFactory>
#include <QDir>
#include <QStandardPaths>
#include <QMessageBox>
#include <QSplashScreen>
#include <QPixmap>
#include <QTimer>
#include <QDebug>
#include "gui/mainwindow.h"
#include "core/controller.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // Configurar información de la aplicación
    app.setApplicationName("Audio Analyzer");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("AudioAnalyzer");
    app.setOrganizationDomain("audioanalyzer.local");

    // Configurar estilo de la aplicación
    app.setStyle(QStyleFactory::create("Fusion"));

    // Configurar paleta oscura opcional
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(25, 25, 25));
    darkPalette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(53, 53, 53));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::BrightText, Qt::red);
    darkPalette.setColor(QPalette::Link, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    // Aplicar tema oscuro (opcional)
    // app.setPalette(darkPalette);

    // Crear directorio de configuración si no existe
    QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir().mkpath(configDir + "/AudioAnalyzer");

    // Mostrar splash screen (opcional)
    QSplashScreen* splash = nullptr;
    /*
    QPixmap pixmap(400, 300);
    pixmap.fill(Qt::black);
    splash = new QSplashScreen(pixmap);
    splash->show();
    splash->showMessage("Loading Audio Analyzer...", Qt::AlignHCenter | Qt::AlignBottom, Qt::white);
    app.processEvents();
    */

    // Crear ventana principal
    MainWindow window;

    // Simular tiempo de carga
    if (splash) {
        QTimer::singleShot(2000, [&]() {
            splash->finish(&window);
            window.show();
        });
    } else {
        window.show();
    }

    // Manejar errores no capturados
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
        QString typeStr;
        switch (type) {
        case QtDebugMsg:    typeStr = "Debug"; break;
        case QtWarningMsg:  typeStr = "Warning"; break;
        case QtCriticalMsg: typeStr = "Critical"; break;
        case QtFatalMsg:    typeStr = "Fatal"; break;
        case QtInfoMsg:     typeStr = "Info"; break;
        }

        QString formatted = QString("[%1] %2:%3 - %4")
                                .arg(typeStr)
                                .arg(context.file ? QFileInfo(context.file).baseName() : "Unknown")
                                .arg(context.line)
                                .arg(msg);

        // Escribir a consola
        fprintf(stderr, "%s\n", formatted.toLocal8Bit().constData());

        // En caso de error fatal, mostrar mensaje
        if (type == QtFatalMsg) {
            QMessageBox::critical(nullptr, "Fatal Error", msg);
        }
    });

    qDebug() << "Audio Analyzer started";
    qDebug() << "Qt version:" << QT_VERSION_STR;
    qDebug() << "Application directory:" << app.applicationDirPath();
    qDebug() << "Config directory:" << configDir;

    ///////////////////////////////////////////////
    /// Creación bd
    ///////////////////////////////////////////////

    // 1) Determina la ruta de la base de datos
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    QString dbPath = dataDir + "/audio_capture.sqlite";

    // 2) Crea y abre la BD
    auto* db = new AudioDb(dbPath, &app);
    if (!db->initialize()) {
        qCritical() << "No se pudo inicializar AudioDb. Saliendo.";
        return -1;
    }

    ///////////////////////////////////////////////
    /// Iniciación componentes clave
    ///////////////////////////////////////////////

    auto* ctrl   = new Controller(db);

    int result = app.exec();

    // Limpiar splash screen si existe
    if (splash) {
        delete splash;
    }

    qDebug() << "Audio Analyzer finished with code:" << result;

    return result;
}
