// Controller.cpp

#include "controller.h"
#include "receivers/audio_receiver.h"
#include "receivers/network_receiver.h"
#include <QThread>
#include <QMetaObject>

Controller::Controller(AudioDb* db, QObject *parent)
    : QObject(parent)
    , m_db(db)
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
    if (!m_receiver || !m_capturing)
        return;

    switch (m_source) {
    case PhysicalAudioInput:
        if (auto phy = qobject_cast<AudioReceiver*>(m_receiver))
            phy->setConfig(m_physCfg);
        break;
    case NetworkAudioInput:
        if (auto net = qobject_cast<NetworkReceiver*>(m_receiver))
            net->setConfig(m_netCfg);
        break;
    }
}

void Controller::startCapture()
{
    if (m_capturing)
        return;

    // 1) Crear y arrancar el receiver
    createReceiver();
    if (!m_receiver)
        return;

    // 2) Crear y arrancar el DSPWorker
    createDspWorker();

    // 3) Conectar señal de audio -> DSP
    connect(m_receiver, &IReceiver::floatChunkReady,
            m_dspWorker, &DSPWorker::processChunk, Qt::QueuedConnection);

    // 4) Conectar señales de receiver -> Controller
    connect(m_receiver, &IReceiver::floatChunkReady,
            this, &Controller::floatChunkReady, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::audioFormatDetected,
            this, &Controller::audioFormatDetected, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::errorOccurred,
            this, &Controller::errorOccurred, Qt::QueuedConnection);
    connect(m_receiver, &IReceiver::finished,
            this, &Controller::finished, Qt::QueuedConnection);

    // 5) Conectar señales de DSP -> Controller (ya hechas en createDspWorker)

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

void Controller::createReceiver()
{
    if (m_receiver)
        return;

    m_captureThread = new QThread(this);

    switch (m_source) {
    case PhysicalAudioInput:
        m_receiver = new AudioReceiver;
        break;
    case NetworkAudioInput:
        m_receiver = new NetworkReceiver;
        break;
    }

    if (!m_receiver) {
        emit errorOccurred("No se pudo crear el receptor de audio");
        m_captureThread->deleteLater();
        m_captureThread = nullptr;
        return;
    }

    m_receiver->moveToThread(m_captureThread);
    connect(m_captureThread, &QThread::finished,
            m_receiver, &QObject::deleteLater);

    m_captureThread->start();
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

void Controller::createDspWorker()
{
    if (m_dspWorker)
        return;

    m_dspWorker = new DSPWorker(m_dspConfig, m_db);
    m_dspThread = new QThread(this);

    m_dspWorker->moveToThread(m_dspThread);
    connect(m_dspThread, &QThread::finished,
            m_dspWorker, &QObject::deleteLater);

    // Re-emisión de eventos DSP -> Controller
    connect(m_dspWorker, &DSPWorker::framesReady,
            this, &Controller::framesReady, Qt::QueuedConnection);
    connect(m_dspWorker, &DSPWorker::statsUpdated,
            this, &Controller::statsUpdated, Qt::QueuedConnection);
    connect(m_dspWorker, &DSPWorker::errorOccurred,
            this, &Controller::errorOccurred, Qt::QueuedConnection);

    m_dspThread->start();
}

void Controller::cleanupDspWorker()
{
    if (!m_dspWorker)
        return;

    // Flush y reset bloqueante
    QMetaObject::invokeMethod(m_dspWorker, "flushResidual",      Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_dspWorker, "reset",              Qt::BlockingQueuedConnection);

    m_dspWorker->disconnect(this);

    m_dspThread->quit();
    m_dspThread->wait();
    m_dspThread->deleteLater();

    m_dspWorker = nullptr;
    m_dspThread = nullptr;
}
