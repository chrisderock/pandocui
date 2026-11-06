#pragma once

#include "pandocformats.h"

#include <QList>
#include <QVariantMap>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;

// Eingabemaske für die Optionen eines einzelnen Ausgabeformats.
// Die Elemente werden aus PandocFormats::optionGroups() erzeugt.
class FormatOptionsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit FormatOptionsWidget(const QString &format, QWidget *parent = nullptr);

    QString format() const { return m_format; }

    QVariantMap values() const;
    void setValues(const QVariantMap &values);
    void resetToDefaults();

    void setCommandPreview(const QString &commandLine);

signals:
    void optionsChanged();

private:
    struct Editor
    {
        OptionDef def;
        QWidget *widget = nullptr;
    };

    QWidget *createEditor(const OptionDef &def, QWidget **valueWidget);
    void setEditorValue(const Editor &editor, const QVariant &value);

    QString m_format;
    QList<Editor> m_editors;
    QLineEdit *m_extraArgs = nullptr;
    QPlainTextEdit *m_preview = nullptr;
};
