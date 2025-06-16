#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include <QWidget>
#include <QStringList>

class QPushButton;
class QComboBox;
class QLabel;

class ControlPanel : public QWidget
{
    Q_OBJECT
public:
    explicit ControlPanel(QWidget* parent = nullptr);

    /**
     * @brief Populate the source selector.
     * @param sources List of user‑friendly source names (e.g., "Microphone", "Network Stream"…)
     */
    void setAvailableSources(const QStringList& sources);

    /**
     * @brief Programmatically select the given index without emitting sourceChanged.
     */
    void selectSource(int index);

signals:
    void startRequested();   //!< User pressed ▶️
    void pauseRequested();   //!< User pressed ⏸️
    void stopRequested();    //!< User pressed ⏹️
    void settingsRequested();//!< User pressed ⚙️
    void sourceChanged(int index);

private slots:
    void handleSourceIndexChanged(int index);

private:
    void buildUi();
    void applyStyle();

    QPushButton* m_btnStart;
    QPushButton* m_btnPause;
    QPushButton* m_btnStop;
    QPushButton* m_btnSettings;
    QComboBox*   m_cmbSources;
};

#endif // CONTROL_PANEL_H
