#include "include/gui/rt_mainwindow.h"
#include <QVBoxLayout>
#include <QDebug>

RTMainWindow::RTMainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();

    // Configurar formato de audio
    m_format.setSampleRate(44100);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    // Listar dispositivos de entrada
    auto inputs = QMediaDevices::audioInputs();
    for (auto& dev : inputs) {
        m_combo->addItem(dev.description());
        m_devices.append(dev);
    }

    connect(m_combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &RTMainWindow::onDeviceChanged);

    if (!m_devices.isEmpty())
        onDeviceChanged(0);
}

RTMainWindow::~RTMainWindow()
{
    m_readTimer.stop();
    if (m_audioSource) {
        m_audioSource->stop();
        m_io->close();
    }
}

void RTMainWindow::setupUi()
{
    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);
    setCentralWidget(central);

    m_combo = new QComboBox(this);
    layout->addWidget(m_combo);

    // Usa tu widget personalizado
    m_waveform = new WaveformWidget(this);
    m_waveform->setMinimumHeight(200);
    layout->addWidget(m_waveform);

    m_readTimer.setInterval(30);
    connect(&m_readTimer, &QTimer::timeout,
            this, &RTMainWindow::readAudioData);
}

void RTMainWindow::onDeviceChanged(int index)
{
    // Detener y limpiar la fuente anterior
    if (m_audioSource) {
        m_readTimer.stop();
        m_audioSource->stop();
        m_io->close();
        delete m_audioSource;
        delete m_audioSink;
        m_audioSource = nullptr;
        m_audioSink   = nullptr;
        m_io           = nullptr;
    }

    // Configurar nuevo dispositivo
    QAudioDevice dev = m_devices[index];
    m_audioSource    = new QAudioSource(dev, m_format, this);
    m_audioSink      = new QAudioSink(QMediaDevices::defaultAudioOutput(), m_format, this);

    m_io = m_audioSource->start();
    m_audioSink->start(m_io);
    m_readTimer.start();

    qDebug() << "Using device:" << dev.description();
}

void RTMainWindow::readAudioData()
{
    if (!m_audioSource || !m_io) return;

    qint64 avail = m_io->bytesAvailable();
    if (avail <= 0) return;

    QByteArray buf(avail, 0);
    qint64 len = m_io->read(buf.data(), avail);
    if (len <= 0) return;

    // Convertir Int16 a float
    const auto* ptr = reinterpret_cast<const int16_t*>(buf.constData());
    int count = int(len / sizeof(int16_t));

    QVector<float> samples;
    samples.reserve(count);
    for (int i = 0; i < count; ++i)
        samples.append(ptr[i] / 32768.0f);

    m_waveform->updateWaveform(samples);
}
