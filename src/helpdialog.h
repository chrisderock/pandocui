#pragma once

#include <QDialog>
#include <QUrl>

class QLabel;
class QLineEdit;
class QTextBrowser;

// Zeigt das eingebettete Benutzerhandbuch (docs/BENUTZERHANDBUCH.md bzw. docs/USER_MANUAL.md)
// in der Sprache der Oberfläche an – mit Sprungmarken aus dem Inhaltsverzeichnis,
// an die Fensterbreite angepassten Bildern und einer Suche.
class HelpDialog : public QDialog
{
    Q_OBJECT

public:
    // language: "de", "en" …; leer = Sprache der Oberfläche
    explicit HelpDialog(QWidget *parent = nullptr, const QString &language = {});

    // Ressource des Handbuchs für eine Sprache (Deutsch oder ansonsten Englisch)
    static QUrl manualUrl(const QString &language);
    // Sprungmarke einer Überschrift wie bei GitHub: "1. Erster Start" → "1-erster-start"
    static QString headingAnchor(const QString &heading);

    QTextBrowser *browser() const { return m_browser; }
    // Springt zur Überschrift mit dieser Sprungmarke; false, wenn es sie nicht gibt.
    bool scrollToAnchor(const QString &anchor);
    // Sucht ab der aktuellen Position (am Ende wieder von vorn); false = nicht gefunden.
    bool findText(const QString &text, bool backward = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void openLink(const QUrl &url);
    void fitImages();

    QTextBrowser *m_browser = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_searchStatus = nullptr;
};
