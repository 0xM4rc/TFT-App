#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#pragma once

#include <QWidget>
#include <QAudioDevice>

class QComboBox;
class QLineEdit;
class QStackedWidget;
class QPushButton;

/**
 * ControlPanel ofrece:
 *  - Selección de tipo de fuente (Physical / Network)
 *  - Selector de dispositivo físico (combo) o URL de red (line-edit)
 *  - Controles de reproducción: Start, Pause, Stop
 *  - Botón de Settings
 *
 * Puedes inyectar un dispositivo inicial y recuperar el actualmente seleccionado.
 */
class ControlPanel : public QWidget
{
    Q_OBJECT
public:
    enum SourceType { Physical = 0, Network = 1 };

    explicit ControlPanel(QWidget* parent = nullptr);
    explicit ControlPanel(const QAudioDevice& initialDevice, QWidget* parent = nullptr);

    /// Establece el dispositivo físico inicialmente seleccionado
    void setInitialPhysicalDevice(const QAudioDevice& dev);
    /// Devuelve el dispositivo físico actualmente seleccionado
    QAudioDevice currentPhysicalDevice() const;
    /// Devuelve la URL introducida
    QString networkUrl() const;

signals:
    void sourceTypeChanged(int type);
    void physicalDeviceChanged(const QAudioDevice& device);
    void networkUrlChanged(const QString& url);

    void startRequested();
    void pauseRequested();
    void stopRequested();
    void settingsRequested();

private slots:
    void switchSourceUi(int index);

private:
    void buildUi();
    void applyStyle();

    QAudioDevice    m_initialDevice;
    bool            m_hasInitialDevice = false;

    QComboBox*      m_cmbSourceType  { nullptr };
    QStackedWidget* m_stack          { nullptr };
    QComboBox*      m_cmbDevices     { nullptr };  // DeviceComboBox subclass
    QLineEdit*      m_leNetworkUrl   { nullptr };

    QPushButton*    m_btnStart       { nullptr };
    QPushButton*    m_btnPause       { nullptr };
    QPushButton*    m_btnStop        { nullptr };
    QPushButton*    m_btnSettings    { nullptr };
};

#endif // CONTROL_PANEL_H
