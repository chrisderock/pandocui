#include "stepnavigator.h"

#include <QEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QVBoxLayout>

namespace {

constexpr int BadgeSize = 28;
constexpr int NavigatorWidth = 230;

QString colorName(const QPalette &palette, QPalette::ColorGroup group, QPalette::ColorRole role)
{
    return palette.color(group, role).name();
}

} // namespace

StepNavigator::StepNavigator(QWidget *parent)
    : QWidget(parent)
{
    setFixedWidth(NavigatorWidth);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addStretch(1);
}

void StepNavigator::setSteps(const QStringList &titles)
{
    auto *layout = static_cast<QVBoxLayout *>(this->layout());
    for (const Step &step : std::as_const(m_steps))
        delete step.frame;
    m_steps.clear();

    for (int i = 0; i < titles.size(); ++i) {
        Step step;
        step.frame = new QFrame;
        step.frame->setObjectName(QStringLiteral("stepFrame"));
        step.frame->setCursor(Qt::PointingHandCursor);
        step.frame->installEventFilter(this);

        step.badge = new QLabel(QString::number(i + 1));
        step.badge->setFixedSize(BadgeSize, BadgeSize);
        step.badge->setAlignment(Qt::AlignCenter);

        step.title = new QLabel(titles.at(i));
        QFont titleFont = step.title->font();
        titleFont.setBold(true);
        step.title->setFont(titleFont);

        step.summary = new QLabel;
        step.summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

        auto *grid = new QGridLayout(step.frame);
        grid->setContentsMargins(8, 6, 8, 6);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(0);
        grid->addWidget(step.badge, 0, 0, 2, 1, Qt::AlignVCenter);
        grid->addWidget(step.title, 0, 1);
        grid->addWidget(step.summary, 1, 1);
        grid->setColumnStretch(1, 1);

        layout->insertWidget(i, step.frame);
        m_steps.append(step);
    }
    for (int i = 0; i < m_steps.size(); ++i)
        updateStyle(i);
}

void StepNavigator::setCurrent(int index)
{
    if (index < 0 || index >= m_steps.size())
        return;
    const int previous = m_current;
    m_current = index;
    updateStyle(previous);
    updateStyle(index);
}

void StepNavigator::setSummary(int index, const QString &text)
{
    if (index < 0 || index >= m_steps.size() || m_steps[index].summaryText == text)
        return;
    m_steps[index].summaryText = text;
    updateSummaryText(index);
}

void StepNavigator::setStepState(int index, bool reachable, bool done)
{
    if (index < 0 || index >= m_steps.size())
        return;
    Step &step = m_steps[index];
    if (step.reachable == reachable && step.done == done)
        return;
    step.reachable = reachable;
    step.done = done;
    updateStyle(index);
}

bool StepNavigator::isReachable(int index) const
{
    return index >= 0 && index < m_steps.size() && m_steps.at(index).reachable;
}

void StepNavigator::updateSummaryText(int index)
{
    Step &step = m_steps[index];
    // Die Leiste hat eine feste Breite: lange Angaben (Pfade, Formatlisten) kürzen
    const int available = NavigatorWidth - BadgeSize - 40;
    step.summary->setText(step.summary->fontMetrics().elidedText(step.summaryText,
                                                                 Qt::ElideMiddle, available));
    step.summary->setToolTip(step.summaryText);
}

void StepNavigator::updateStyle(int index)
{
    if (index < 0 || index >= m_steps.size())
        return;
    Step &step = m_steps[index];
    const QPalette pal = palette();
    const bool current = index == m_current;

    QString frameBg = QStringLiteral("transparent");
    QString titleColor = colorName(pal, QPalette::Active, QPalette::WindowText);
    QString summaryColor = colorName(pal, QPalette::Disabled, QPalette::WindowText);
    QString badgeBg = colorName(pal, QPalette::Active, QPalette::Mid);
    QString badgeFg = colorName(pal, QPalette::Active, QPalette::Light);

    if (current) {
        frameBg = colorName(pal, QPalette::Active, QPalette::Highlight);
        titleColor = colorName(pal, QPalette::Active, QPalette::HighlightedText);
        summaryColor = titleColor;
        badgeBg = colorName(pal, QPalette::Active, QPalette::HighlightedText);
        badgeFg = colorName(pal, QPalette::Active, QPalette::Highlight);
    } else if (step.done) {
        badgeBg = QStringLiteral("#2e8b57");
        badgeFg = QStringLiteral("#ffffff");
    } else if (!step.reachable) {
        titleColor = colorName(pal, QPalette::Disabled, QPalette::WindowText);
        badgeBg = colorName(pal, QPalette::Active, QPalette::Midlight);
        badgeFg = colorName(pal, QPalette::Disabled, QPalette::WindowText);
    }

    step.frame->setStyleSheet(QStringLiteral("#stepFrame { background: %1; border-radius: 6px; }")
                                  .arg(frameBg));
    step.badge->setText(step.done && !current ? QStringLiteral("✓") : QString::number(index + 1));
    step.badge->setStyleSheet(QStringLiteral("QLabel { background: %1; color: %2; border-radius: %3px;"
                                             " font-weight: bold; }")
                                  .arg(badgeBg, badgeFg)
                                  .arg(BadgeSize / 2));
    step.title->setStyleSheet(QStringLiteral("color: %1;").arg(titleColor));
    step.summary->setStyleSheet(QStringLiteral("color: %1;").arg(summaryColor));
    step.frame->setCursor(step.reachable ? Qt::PointingHandCursor : Qt::ForbiddenCursor);
    updateSummaryText(index);
}

bool StepNavigator::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonRelease && isEnabled()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::LeftButton) {
            for (int i = 0; i < m_steps.size(); ++i) {
                if (m_steps.at(i).frame == watched && m_steps.at(i).reachable) {
                    emit stepClicked(i);
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void StepNavigator::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange)
        scheduleRestyle();
}

void StepNavigator::scheduleRestyle()
{
    // Palettenänderungen kommen u. a. während Qt die Widgets "poliert" (z. B. beim
    // ersten Anzeigen). Stylesheets daher nicht mitten darin setzen, sondern erst nach
    // Rückkehr in die Ereignisschleife und mehrere Änderungen zusammengefasst.
    if (m_restylePending)
        return;
    m_restylePending = true;
    QMetaObject::invokeMethod(this, [this] {
        m_restylePending = false;
        for (int i = 0; i < m_steps.size(); ++i)
            updateStyle(i);
    }, Qt::QueuedConnection);
}
