#include "include/gui/control_panel.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QIcon>
#include <QStyle>
#include <QApplication>

ControlPanel::ControlPanel(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    applyStyle();
}

void ControlPanel::setAvailableSources(const QStringList& sources)
{
    bool blocked = m_cmbSources->blockSignals(true);
    m_cmbSources->clear();
    m_cmbSources->addItems(sources);
    m_cmbSources->blockSignals(blocked);
}

void ControlPanel::selectSource(int index)
{
    bool blocked = m_cmbSources->blockSignals(true);
    m_cmbSources->setCurrentIndex(index);
    m_cmbSources->blockSignals(blocked);
}

void ControlPanel::buildUi()
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(6);

    m_cmbSources  = new QComboBox(this);
    m_btnStart    = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),  "", this);
    m_btnPause    = new QPushButton(style()->standardIcon(QStyle::SP_MediaPause), "", this);
    m_btnStop     = new QPushButton(style()->standardIcon(QStyle::SP_MediaStop),  "", this);
    m_btnSettings = new QPushButton(style()->standardIcon(QStyle::SP_FileDialogDetailedView), "", this);

    m_btnStart->setToolTip(tr("Start (Space)"));
    m_btnPause->setToolTip(tr("Pause"));
    m_btnStop->setToolTip(tr("Stop"));
    m_btnSettings->setToolTip(tr("Settings"));

    m_btnStart->setShortcut(Qt::Key_Space);

    layout->addWidget(m_cmbSources, 1);
    layout->addWidget(m_btnStart);
    layout->addWidget(m_btnPause);
    layout->addWidget(m_btnStop);
    layout->addStretch();
    layout->addWidget(m_btnSettings);

    // Connections
    connect(m_btnStart,    &QPushButton::clicked, this, &ControlPanel::startRequested);
    connect(m_btnPause,    &QPushButton::clicked, this, &ControlPanel::pauseRequested);
    connect(m_btnStop,     &QPushButton::clicked, this, &ControlPanel::stopRequested);
    connect(m_btnSettings, &QPushButton::clicked, this, &ControlPanel::settingsRequested);
    connect(m_cmbSources,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ControlPanel::handleSourceIndexChanged);
}

void ControlPanel::applyStyle()
{
    // A lightweight dark theme—adjust to taste or replace with your global stylesheet.
    QString css = R"(
        QWidget {
            background-color: #202124;
            color: #E8EAED;
            font-size: 12px;
        }
        QComboBox {
            background-color: #303134;
            border: 1px solid #5f6368;
            padding: 4px 6px;
            border-radius: 4px;
        }
        QPushButton {
            background-color: #303134;
            border: 1px solid #5f6368;
            padding: 4px;
            border-radius: 4px;
        }
        QPushButton:hover {
            background-color: #3c4043;
        }
        QPushButton:pressed {
            background-color: #5f6368;
        }
    )";
    setStyleSheet(css);
}

void ControlPanel::handleSourceIndexChanged(int index)
{
    emit sourceChanged(index);
}
