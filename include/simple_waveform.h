#pragma once
#include <QWidget>
#include <QVector>
#include <QMutex>

class SimpleWaveformWidget : public QWidget {
    Q_OBJECT

public:
    explicit SimpleWaveformWidget(QWidget* parent = nullptr);

public slots:
    void updateWaveform(const QVector<float>& samples);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVector<float> m_waveform;
    QMutex         m_mutex;
};
