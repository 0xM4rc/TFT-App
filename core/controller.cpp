// Controller.cpp

#include "controller.h"
#include "receivers/audio_receiver.h"
#include "receivers/network_receiver.h"
#include <QThread>
#include <QMetaObject>
#include "core/audio_db.h"
#include <QDir>
#include <QUuid>
#include <QCoreApplication>
#include "views/waveform_render.h"


Controller::Controller(QObject *parent)
    : QObject(parent)
{
}

Controller::~Controller()
{
    stopCapture();
}

void Controller::setAudioSource(AudioSource src)
{
    if (m_source == src)
        return;

    stopCapture();
    m_source = src;
    emit audioSourceChanged(m_source);
}

void Controller::setPhysicalConfig(const PhysicalInputConfig &cfg)
{
    if (int err = cfg.isValid(); err != 0) {
        emit errorOccurred(QString("PhysicalConfig inválido (%1)").arg(err));
        return;
    }
    m_physCfg = cfg;
    applyConfigToCurrentReceiver();
}

void Controller::setNetworkConfig(const NetworkInputConfig &cfg)
{
    // Aquí podrías añadir validación similar si lo deseas
    m_netCfg = cfg;
    applyConfigToCurrentReceiver();
}

void Controller::applyConfigToCurrentReceiver()
{
    if (!m_receiver || m_capturing) return;

    switch (m_source) {
    case PhysicalAudioInput: {
        if (auto phy = qobject_cast<AudioReceiver*>(m_receiver)) {
            PhysicalInputConfig cfg = m_physCfg; // copia por valor
            if (m_receiver->thread() == QThread::currentThread()) {
                phy->setConfig(cfg);
            } else {
                QMetaObject::invokeMethod(
                    phy,
                    [phy, cfg]() { phy->setConfig(cfg); },
                    Qt::QueuedConnection
                    );
            }
        }
        break;
    }
    case NetworkAudioInput: {
        if (auto net = qobject_cast<NetworkReceiver*>(m_receiver)) {
            NetworkInputConfig cfg = m_netCfg; // copia por valor
            if (m_receiver->thread() == QThread::currentThread()) {
                net->setConfig(cfg);
            } else {
                QMetaObject::invokeMethod(
                    net,
                    [net, cfg]() { net->setConfig(cfg); },
                    Qt::QueuedConnection
                    );
            }
        }
        break;
    }
    }
}


void Controller::startCapture()
{
    if (m_capturing)
        return;

    qDebug() << "START CAPTURE";

    // 1) (Re)crear receiver en su hilo
    if (!createReceiver()) {
        emit errorOccurred("No se pudo crear el receptor de audio");
        return;
    }

    // 2) Rotar/crear DB (no inicializar aquí)
    setupDatabase();  // esto aplicará la lógica m_rotateDbPerSession y emitirá databaseChanged(path)

    // 3) Crear DSPWorker + mover DB al hilo DSP + initialize() en SU hilo
    if (!createDspWorker()) {       // haz que devuelva bool: false si initialize() falla
        cleanupReceiver();
        emit errorOccurred("No se pudo inicializar el DSP/DB");
        return;
    }

    // 4) Conectar receiver -> DSP (audio crudo a procesamiento)
    connect(m_receiver, &IReceiver::floatChunkReady,
            m_dspWorker, &DSPWorker::processChunk,
            Qt::QueuedConnection);

    // 5) Conectar eventos del receiver hacia Controller (opcional re-emisión)
    connect(m_receiver, &IReceiver::floatChunkReady,
            this, &Controller::floatChunkReady, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::audioFormatDetected,
            this, &Controller::audioFormatDetected, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::errorOccurred,
            this, &Controller::errorOccurred, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::finished,
            this, &Controller::finished, Qt::QueuedConnection);

    // 6) Arrancar captura en el hilo del receiver
    QMetaObject::invokeMethod(m_receiver, "start", Qt::QueuedConnection);

    m_capturing = true;
    emit capturingChanged(true);
}


void Controller::stopCapture()
{
    if (!m_capturing)
        return;

    // 1) Desconectar audio -> DSP
    disconnect(m_receiver, &IReceiver::floatChunkReady,
               m_dspWorker, &DSPWorker::processChunk);

    // 2) Limpiar DSPWorker
    cleanupDspWorker();

    // 3) Limpiar Receiver
    cleanupReceiver();

    m_capturing = false;
    emit capturingChanged(false);
}


bool Controller::createReceiver()
{
    if (m_receiver)
        return true; // ya creado, no hay error

    m_captureThread = new QThread(this);

    switch (m_source) {
    case PhysicalAudioInput: {
        m_receiver = new AudioReceiver;
        PhysicalInputConfig pcfg;
        if (currentPhysicalConfig(pcfg)) {
            static_cast<AudioReceiver*>(m_receiver)->setConfig(pcfg);
        }
        break;
    }
    case NetworkAudioInput: {
        m_receiver = new NetworkReceiver;
        NetworkInputConfig ncfg;
        if (currentNetworkConfig(ncfg)) {
            static_cast<NetworkReceiver*>(m_receiver)->setConfig(ncfg);
        }
        break;
    }
    default:
        m_receiver = nullptr;
        break;
    }

    if (!m_receiver) {
        emit errorOccurred("No se pudo crear el receptor de audio");
        m_captureThread->deleteLater();
        m_captureThread = nullptr;
        return false;
    }

    m_receiver->moveToThread(m_captureThread);
    connect(m_captureThread, &QThread::finished,
            m_receiver, &QObject::deleteLater);

    m_captureThread->start();

    return true;
}



void Controller::cleanupReceiver()
{
    if (!m_receiver)
        return;

    // Llamada bloqueante para asegurar que stop() finaliza
    QMetaObject::invokeMethod(m_receiver, "stop", Qt::BlockingQueuedConnection);

    m_receiver->disconnect(this);

    m_captureThread->quit();
    m_captureThread->wait();
    m_captureThread->deleteLater();

    m_receiver = nullptr;
    m_captureThread = nullptr;
}

void Controller::setupDatabase()
{
    if (m_rotateDbPerSession) {
        // Si ya había una DB previa, la eliminamos correctamente
        if (m_db) {
            m_db->deleteLater();
            m_db = nullptr;
        }
        m_currentDbPath = makeRandomDbPath();
        m_db = new AudioDb(m_currentDbPath); // sin parent: la moveremos de hilo
        emit databaseChanged(m_currentDbPath);

    } else {
        // Ruta persistente: solo crear si aún no existe instancia
        if (!m_db) {
            m_currentDbPath = QCoreApplication::applicationDirPath() + "/audio_capture.db";
            m_db = new AudioDb(m_currentDbPath);
            emit databaseChanged(m_currentDbPath);
        }
    }
}


bool Controller::createDspWorker()
{
    if (m_dspWorker)
        return true; // ya existe, no hay error

    m_dspThread = new QThread(this);

    // mover DB al hilo DSP
    if (!m_db) {
        setupDatabase();
    }
    if (!m_db) {
        qCritical() << "Controller: No se pudo crear la base de datos";
        return false;
    }
    m_db->moveToThread(m_dspThread);

    m_dspWorker = new DSPWorker(m_dspConfig, m_db);
    if (!m_dspWorker) {
        qCritical() << "Controller: No se pudo crear el DSPWorker";
        m_db->moveToThread(QCoreApplication::instance()->thread()); // devolver DB al hilo principal
        return false;
    }
    m_dspWorker->moveToThread(m_dspThread);

    connect(m_dspThread, &QThread::finished, m_dspWorker, &QObject::deleteLater);
    connect(m_dspThread, &QThread::finished, m_db,        &QObject::deleteLater);

    // señales DSP -> Controller
    connect(m_dspWorker, &DSPWorker::framesReady,
            this, &Controller::onDspFramesReady, Qt::QueuedConnection);

    connect(m_dspWorker, &DSPWorker::framesReady,
            this, &Controller::framesReady, Qt::QueuedConnection);

    connect(m_dspWorker, &DSPWorker::statsUpdated, this, &Controller::statsUpdated, Qt::QueuedConnection);
    connect(m_dspWorker, &DSPWorker::errorOccurred,this, &Controller::errorOccurred,Qt::QueuedConnection);

    m_dspThread->start();

    bool ok = false;
    QMetaObject::invokeMethod(m_db, [&](){
        ok = m_db->initialize();
    }, Qt::BlockingQueuedConnection);

    if (!ok) {
        qCritical() << "Controller: No se pudo abrir la base de datos";
        m_dspThread->quit();
        m_dspThread->wait();
        m_dspThread->deleteLater();
        m_dspThread = nullptr;

        m_dspWorker->deleteLater();
        m_dspWorker = nullptr;

        m_db->deleteLater();
        m_db = nullptr;
        return false;
    }

    return true;
}


void Controller::cleanupDspWorker()
{
    if (!m_dspWorker)
        return;

    // Flush y reset bloqueante
    QMetaObject::invokeMethod(m_dspWorker, "flushResidual", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_dspWorker, "reset",         Qt::BlockingQueuedConnection);

    // Cerrar conexión SQLite EN SU HILO
    if (m_db) {
        QMetaObject::invokeMethod(m_db, "shutdown", Qt::BlockingQueuedConnection);
    }

    m_dspWorker->disconnect(this);

    m_dspThread->quit();
    m_dspThread->wait();

    // destruir objetos que viven en el hilo DSP
    if (m_db)        { m_db->deleteLater();        m_db = nullptr; }
    if (m_dspThread) { m_dspThread->deleteLater(); m_dspThread = nullptr; }
    m_dspWorker = nullptr;

    // limpiar estado de sesión y notificar a la UI
    m_currentDbPath.clear();
    emit databaseChanged(QString());
}

void Controller::onDspFramesReady(const QVector<FrameData>& frames)
{
    if (!m_waveView) return;
    QVector<FrameData> copy = frames;

    QMetaObject::invokeMethod(
        m_waveView.data(),
        [vw = m_waveView.data(), copy]() {
            if (!vw) return;
            vw->processFrames(copy);
        },
        Qt::QueuedConnection
        );

    if (m_specView) {
        QVector<FrameData> copy2 = frames;
        QMetaObject::invokeMethod(
            m_specView.data(),
            [specView = m_specView.data(), copy2]() {
                if (specView) specView->processFrames(copy2);
            },
            Qt::QueuedConnection);
    }
}

void Controller::clearWaveform()
{
    if (!m_waveView) return;
    QMetaObject::invokeMethod(
        m_waveView.data(),
        [vw = m_waveView.data()]() {
            if (vw) vw->clear();
        },
        Qt::QueuedConnection
        );
}

void Controller::pauseWaveform(bool paused)
{
    if (!m_waveView) return;
    QMetaObject::invokeMethod(
        m_waveView.data(),
        [vw = m_waveView.data(), paused]() {
            if (vw) vw->setPaused(paused);
        },
        Qt::QueuedConnection
        );
}

void Controller::setWaveformZoom(float z)
{
    if (!m_waveView) return;
    QMetaObject::invokeMethod(
        m_waveView.data(),
        [vw = m_waveView.data(), z]() {
            if (vw) vw->setZoom(z);
        },
        Qt::QueuedConnection
        );
}

void Controller::setWaveformConfig(const WaveformConfig& cfg)
{
    if (!m_waveView) return;
    QMetaObject::invokeMethod(
        m_waveView.data(),
        [vw = m_waveView.data(), cfg]() {
            if (vw) vw->setConfig(cfg);
        },
        Qt::QueuedConnection
        );
}

void Controller::setWaveformView(WaveformRenderer* view)
{
    m_waveView = view;
    // (Opcional) conecta una señal para enviar frames sin invokeMethod manual:
    // connect(this, &Controller::framesReady, m_waveView, &WaveformRenderer::processFrames, Qt::QueuedConnection);
}

bool Controller::currentPhysicalConfig(PhysicalInputConfig& out) const {
    if (m_source != PhysicalAudioInput) return false;
    out = m_physCfg; return true;
}

bool Controller::currentNetworkConfig(NetworkInputConfig& out) const {
    if (m_source != NetworkAudioInput) return false;
    out = m_netCfg; return true;
}

// SpectrogramRender

void Controller::clearSpectrogram()
{
    if (!m_specView) return;
    QMetaObject::invokeMethod(
        m_specView.data(),
        [sv = m_specView.data()](){
            if (sv) sv->clear();
        },
        Qt::QueuedConnection);
}

void Controller::pauseSpectrogram(bool paused)
{
    if (!m_specView) return;
    QMetaObject::invokeMethod(
        m_specView.data(),
        [sv = m_specView.data(), paused](){
            if (sv) sv->pause(paused);
        },
        Qt::QueuedConnection);
}

void Controller::setSpectrogramConfig(const SpectrogramConfig& cfg)
{
    if (!m_specView) return;
    QMetaObject::invokeMethod(
        m_specView.data(),
        [sv = m_specView.data(), cfg](){
            if (sv) sv->setConfig(cfg);
        },
        Qt::QueuedConnection);
}


// Utils
QString Controller::makeRandomDbPath()
{
    QDir baseDir(QCoreApplication::applicationDirPath());
    if (!baseDir.exists("tmp")) {
        baseDir.mkdir("tmp");
    }
    QString uniqueName = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString fileName = uniqueName + ".db";
    QString fullPath = baseDir.filePath("tmp/" + fileName);
    return fullPath;
}

void Controller::setRotateDbPerSession(bool on) {
    m_rotateDbPerSession = on;
}
