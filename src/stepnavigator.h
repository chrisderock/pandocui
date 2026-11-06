#pragma once

#include <QList>
#include <QWidget>

class QLabel;
class QFrame;

// Senkrechte Schrittleiste für den Wizard: Nummer bzw. Haken, Titel und eine
// Kurzfassung der getroffenen Auswahl je Schritt. Erreichbare Schritte sind anklickbar.
class StepNavigator : public QWidget
{
    Q_OBJECT

public:
    explicit StepNavigator(QWidget *parent = nullptr);

    void setSteps(const QStringList &titles);
    int count() const { return int(m_steps.size()); }

    void setCurrent(int index);
    int current() const { return m_current; }

    void setSummary(int index, const QString &text);
    void setStepState(int index, bool reachable, bool done);
    bool isReachable(int index) const;

signals:
    void stepClicked(int index);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    struct Step
    {
        QFrame *frame = nullptr;
        QLabel *badge = nullptr;
        QLabel *title = nullptr;
        QLabel *summary = nullptr;
        QString summaryText;
        bool reachable = true;
        bool done = false;
    };

    void updateStyle(int index);
    void updateSummaryText(int index);
    void scheduleRestyle();

    QList<Step> m_steps;
    int m_current = 0;
    bool m_restylePending = false;
};
