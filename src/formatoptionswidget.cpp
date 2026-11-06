#include "formatoptionswidget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

FormatOptionsWidget::FormatOptionsWidget(const QString &format, QWidget *parent)
    : QWidget(parent)
    , m_format(format)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumHeight(60);
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 8, 0);

    const QList<OptionGroup> groups = PandocFormats::optionGroups(format);
    for (const OptionGroup &group : groups) {
        auto *box = new QGroupBox(group.title);
        auto *form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const OptionDef &def : group.options) {
            QWidget *valueWidget = nullptr;
            QWidget *row = createEditor(def, &valueWidget);
            row->setToolTip(def.toolTip);
            if (def.type == OptionDef::Type::Bool)
                form->addRow(row);
            else
                form->addRow(def.label + QLatin1Char(':'), row);
            m_editors.append({def, valueWidget});
        }
        contentLayout->addWidget(box);
    }

    auto *extraBox = new QGroupBox(tr("Weitere pandoc-Argumente"));
    auto *extraLayout = new QVBoxLayout(extraBox);
    m_extraArgs = new QLineEdit;
    m_extraArgs->setPlaceholderText(tr("z. B. --shift-heading-level-by=1 --dpi=150"));
    m_extraArgs->setToolTip(tr("Beliebige zusätzliche Argumente, die unverändert an pandoc "
                               "übergeben werden. Werte mit Leerzeichen in Anführungszeichen setzen."));
    connect(m_extraArgs, &QLineEdit::textChanged, this, &FormatOptionsWidget::optionsChanged);
    extraLayout->addWidget(m_extraArgs);
    contentLayout->addWidget(extraBox);
    contentLayout->addStretch(1);

    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    auto *previewHeader = new QHBoxLayout;
    previewHeader->addWidget(new QLabel(tr("Befehlsvorschau:")));
    previewHeader->addStretch(1);
    auto *resetButton = new QPushButton(tr("Auf Standard zurücksetzen"));
    connect(resetButton, &QPushButton::clicked, this, &FormatOptionsWidget::resetToDefaults);
    previewHeader->addWidget(resetButton);
    outer->addLayout(previewHeader);

    m_preview = new QPlainTextEdit;
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMaximumHeight(72);
    m_preview->setMinimumHeight(40);
    m_preview->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    outer->addWidget(m_preview);

    resetToDefaults();
}

QWidget *FormatOptionsWidget::createEditor(const OptionDef &def, QWidget **valueWidget)
{
    switch (def.type) {
    case OptionDef::Type::Bool: {
        auto *check = new QCheckBox(def.label);
        connect(check, &QCheckBox::toggled, this, &FormatOptionsWidget::optionsChanged);
        *valueWidget = check;
        return check;
    }
    case OptionDef::Type::Int: {
        auto *spin = new QSpinBox;
        spin->setRange(0, def.maximum);
        spin->setSpecialValueText(tr("Standard"));
        spin->setMaximumWidth(120);
        connect(spin, &QSpinBox::valueChanged, this, [this, spin, min = def.minimum](int v) {
            // Werte zwischen 0 ("Standard") und dem Minimum überspringen
            if (v > 0 && v < min) {
                spin->setValue(min);
                return;
            }
            emit optionsChanged();
        });
        *valueWidget = spin;
        return spin;
    }
    case OptionDef::Type::Choice: {
        auto *combo = new QComboBox;
        combo->addItem(tr("Standard"), QString());
        for (const QString &choice : def.choices)
            combo->addItem(choice, choice);
        connect(combo, &QComboBox::currentIndexChanged, this, &FormatOptionsWidget::optionsChanged);
        *valueWidget = combo;
        return combo;
    }
    case OptionDef::Type::Text: {
        auto *edit = new QLineEdit;
        edit->setPlaceholderText(def.placeholder);
        edit->setClearButtonEnabled(true);
        connect(edit, &QLineEdit::textChanged, this, &FormatOptionsWidget::optionsChanged);
        *valueWidget = edit;
        return edit;
    }
    case OptionDef::Type::File: {
        auto *container = new QWidget;
        auto *layout = new QHBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit;
        edit->setPlaceholderText(def.placeholder.isEmpty() ? tr("keine Datei") : def.placeholder);
        edit->setClearButtonEnabled(true);
        auto *button = new QToolButton;
        button->setText(QStringLiteral("…"));
        button->setToolTip(tr("Datei auswählen"));
        layout->addWidget(edit, 1);
        layout->addWidget(button);
        connect(edit, &QLineEdit::textChanged, this, &FormatOptionsWidget::optionsChanged);
        connect(button, &QToolButton::clicked, this, [this, edit, def] {
            const QString start = edit->text().isEmpty() ? QString()
                                                         : QFileInfo(edit->text()).absolutePath();
            const QString file = QFileDialog::getOpenFileName(this, def.label, start, def.fileFilter);
            if (!file.isEmpty())
                edit->setText(QDir::toNativeSeparators(file));
        });
        *valueWidget = edit;
        return container;
    }
    }
    *valueWidget = nullptr;
    return new QWidget;
}

void FormatOptionsWidget::setEditorValue(const Editor &editor, const QVariant &value)
{
    switch (editor.def.type) {
    case OptionDef::Type::Bool:
        static_cast<QCheckBox *>(editor.widget)->setChecked(value.toBool());
        break;
    case OptionDef::Type::Int:
        static_cast<QSpinBox *>(editor.widget)->setValue(value.toInt());
        break;
    case OptionDef::Type::Choice: {
        auto *combo = static_cast<QComboBox *>(editor.widget);
        const int index = combo->findData(value.toString());
        combo->setCurrentIndex(index < 0 ? 0 : index);
        break;
    }
    case OptionDef::Type::Text:
    case OptionDef::Type::File:
        static_cast<QLineEdit *>(editor.widget)->setText(value.toString());
        break;
    }
}

QVariantMap FormatOptionsWidget::values() const
{
    QVariantMap map;
    for (const Editor &editor : m_editors) {
        switch (editor.def.type) {
        case OptionDef::Type::Bool:
            map.insert(editor.def.id, static_cast<QCheckBox *>(editor.widget)->isChecked());
            break;
        case OptionDef::Type::Int:
            map.insert(editor.def.id, static_cast<QSpinBox *>(editor.widget)->value());
            break;
        case OptionDef::Type::Choice:
            map.insert(editor.def.id, static_cast<QComboBox *>(editor.widget)->currentData());
            break;
        case OptionDef::Type::Text:
        case OptionDef::Type::File:
            map.insert(editor.def.id, static_cast<QLineEdit *>(editor.widget)->text());
            break;
        }
    }
    map.insert(QLatin1String(PandocFormats::extraArgumentsKey()), m_extraArgs->text());
    return map;
}

void FormatOptionsWidget::setValues(const QVariantMap &values)
{
    {
        const QSignalBlocker blocker(this);
        for (const Editor &editor : std::as_const(m_editors))
            setEditorValue(editor, values.value(editor.def.id, editor.def.defaultValue));
        m_extraArgs->setText(values.value(QLatin1String(PandocFormats::extraArgumentsKey())).toString());
    }
    emit optionsChanged();
}

void FormatOptionsWidget::resetToDefaults()
{
    setValues({});
}

void FormatOptionsWidget::setCommandPreview(const QString &commandLine)
{
    if (m_preview->toPlainText() != commandLine)
        m_preview->setPlainText(commandLine);
}
