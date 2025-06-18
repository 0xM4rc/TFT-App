#include "include/gui/control_panel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QStackedWidget>
#include <QStyle>
#include <QMediaDevices>
#include <QVariant>

Q_DECLARE_METATYPE(QAudioDevice)

/**
 * Subclase de QComboBox que refresca su lista de QAudioDevice
 * y selecciona el default justo antes de abrir el popup.
 */
class DeviceComboBox : public QComboBox
{
public:
    explicit DeviceComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
        qRegisterMetaType<QAudioDevice>();
    }

    /// Refresca lista y selecciona el default
    void refreshDevices(const QAudioDevice* initial = nullptr)
    {
        blockSignals(true);
        clear();

        const auto inputs     = QMediaDevices::audioInputs();
        const auto defaultDev = QMediaDevices::defaultAudioInput();
        int defaultIndex      = -1;

        for (int i = 0; i < inputs.size(); ++i) {
            const auto& dev = inputs.at(i);
            addItem(dev.description(), QVariant::fromValue(dev));
            // si coincide con initial, recuerda índice
            if (initial && dev == *initial)
                defaultIndex = i;
            else if (!initial && dev == defaultDev)
                defaultIndex = i;
        }

        if (defaultIndex >= 0)
            setCurrentIndex(defaultIndex);

        blockSignals(false);
    }

protected:
    void showPopup() override
    {
        refreshDevices(nullptr);
        QComboBox::showPopup();
    }
};

ControlPanel::ControlPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();
    // inicializa la lista y selecciona por defecto
    static_cast<DeviceComboBox*>(m_cmbDevices)->refreshDevices(nullptr);
}

ControlPanel::ControlPanel(const QAudioDevice& initialDevice, QWidget* parent)
    : QWidget(parent)
    , m_initialDevice(initialDevice)
    , m_hasInitialDevice(true)
{
    buildUi();
    applyStyle();
    // inicializa la lista y selecciona el dispositivo inyectado
    static_cast<DeviceComboBox*>(m_cmbDevices)->refreshDevices(&m_initialDevice);
}

void ControlPanel::setInitialPhysicalDevice(const QAudioDevice& dev)
{
    m_initialDevice    = dev;
    m_hasInitialDevice = true;
    static_cast<DeviceComboBox*>(m_cmbDevices)->refreshDevices(&m_initialDevice);
}

QAudioDevice ControlPanel::currentPhysicalDevice() const
{
    return m_cmbDevices->currentData().value<QAudioDevice>();
}

QString ControlPanel::networkUrl() const
{
    return m_leNetworkUrl->text();
}

void ControlPanel::buildUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    // Fuente: Physical / Network
    m_cmbSourceType = new QComboBox(this);
    m_cmbSourceType->addItems({tr("Physical"), tr("Network")});
    layout->addWidget(m_cmbSourceType);

    // Stacked: dispositivo combo / URL line-edit
    m_stack        = new QStackedWidget(this);
    m_cmbDevices   = new DeviceComboBox(this);
    m_leNetworkUrl = new QLineEdit(this);

    m_stack->addWidget(m_cmbDevices);    // página 0
    m_stack->addWidget(m_leNetworkUrl);  // página 1
    layout->addWidget(m_stack, 1);

    // Botones
    m_btnStart    = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),  "", this);
    m_btnPause    = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), "", this);
    m_btnStop     = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop),  "", this);
    m_btnSettings = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "", this);

    layout->addWidget(m_btnStart);
    layout->addWidget(m_btnPause);
    layout->addWidget(m_btnStop);
    layout->addStretch();
    layout->addWidget(m_btnSettings);

    // Conexiones
    connect(m_cmbSourceType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::switchSourceUi);
    connect(m_cmbSourceType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::sourceTypeChanged);

    connect(m_cmbDevices, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){
                QAudioDevice dev = m_cmbDevices->itemData(idx).value<QAudioDevice>();
                emit physicalDeviceChanged(dev);
            });

    connect(m_leNetworkUrl, &QLineEdit::editingFinished, this, [this](){
        emit networkUrlChanged(m_leNetworkUrl->text());
    });

    connect(m_btnStart,    &QPushButton::clicked, this, &ControlPanel::startRequested);
    connect(m_btnPause,    &QPushButton::clicked, this, &ControlPanel::pauseRequested);
    connect(m_btnStop,     &QPushButton::clicked, this, &ControlPanel::stopRequested);
    connect(m_btnSettings, &QPushButton::clicked, this, &ControlPanel::settingsRequested);
}

void ControlPanel::applyStyle()
{
    static const QString css = R"styled(
        QWidget { background-color:#1E1F21; color:#E8EAED; }
        QComboBox,QLineEdit,QPushButton {
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                                       stop:0 #37383A, stop:1 #2A2B2D);
            border:1px solid #5F6368;
            border-radius:6px;
            padding:4px 8px;
        }
        QComboBox::drop-down {
            subcontrol-origin:padding;
            subcontrol-position:top right;
            width:20px; border-left:1px solid #5F6368;
        }
        QComboBox::down-arrow {
            image:url(:/icons/arrow_down.svg);
            width:12px; height:12px;
        }
        QComboBox QListView {
            background:#2A2B2D;
            border:1px solid #5F6368;
        }
        QPushButton:hover  { background:#3C4043; }
        QPushButton:pressed{ background:#5F6368; }
    )styled";
    setStyleSheet(css);
}

void ControlPanel::switchSourceUi(int index)
{
    m_stack->setCurrentIndex(index);
}
