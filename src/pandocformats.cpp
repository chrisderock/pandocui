#include "pandocformats.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

namespace PandocFormats {

namespace {

// Übersetzungskontext "PandocFormats" für die freien Funktionen dieses Namensraums
struct Tr
{
    Q_DECLARE_TR_FUNCTIONS(PandocFormats)
};

// ---------------------------------------------------------------------------
// Erkennung des Eingabeformats
// ---------------------------------------------------------------------------

const QHash<QString, QString> &inputExtensionMap()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> m = {
            {"md", "markdown"},    {"markdown", "markdown"}, {"mkd", "markdown"},
            {"mkdn", "markdown"},  {"mdown", "markdown"},    {"mdwn", "markdown"},
            {"mdtxt", "markdown"}, {"mdtext", "markdown"},   {"rmd", "markdown"},
            {"qmd", "markdown"},   {"txt", "markdown"},      {"text", "markdown"},
            {"htm", "html"},       {"html", "html"},         {"xhtml", "html"},
            {"tex", "latex"},      {"latex", "latex"},       {"ltx", "latex"},
            {"docx", "docx"},      {"odt", "odt"},           {"epub", "epub"},
            {"rtf", "rtf"},        {"pptx", "pptx"},         {"xlsx", "xlsx"},
            {"rst", "rst"},        {"org", "org"},           {"textile", "textile"},
            {"ipynb", "ipynb"},    {"csv", "csv"},           {"tsv", "tsv"},
            {"typ", "typst"},      {"t2t", "t2t"},
            {"wiki", "mediawiki"}, {"mediawiki", "mediawiki"},
            {"opml", "opml"},      {"fb2", "fb2"},           {"bib", "biblatex"},
            {"ris", "ris"},        {"muse", "muse"},         {"dbk", "docbook"},
            {"docbook", "docbook"},{"native", "native"},     {"adoc", "asciidoc"},
            {"asciidoc", "asciidoc"}, {"dj", "djot"},        {"djot", "djot"},
            {"pod", "pod"},        {"man", "man"},           {"roff", "man"},
            {"creole", "creole"},  {"jira", "jira"},
        };
        for (int section = 1; section <= 9; ++section)
            m.insert(QString::number(section), QStringLiteral("man"));
        return m;
    }();
    return map;
}

QByteArray readHead(const QString &path, qint64 size = 8192)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.read(size).toLower();
}

QString sniffXml(const QString &path)
{
    const QByteArray head = readHead(path);
    if (head.contains("<opml"))
        return QStringLiteral("opml");
    if (head.contains("<fictionbook"))
        return QStringLiteral("fb2");
    if (head.contains("jats") || head.contains("-//nlm//"))
        return QStringLiteral("jats");
    if (head.contains("<xml") && head.contains("<records"))
        return QStringLiteral("endnotexml");
    return QStringLiteral("docbook");
}

QString sniffJson(const QString &path)
{
    const QByteArray head = readHead(path);
    if (head.contains("\"nbformat\""))
        return QStringLiteral("ipynb");
    if (head.contains("\"pandoc-api-version\""))
        return QStringLiteral("json");
    if (head.trimmed().startsWith('['))
        return QStringLiteral("csljson");
    return QStringLiteral("json");
}

// ---------------------------------------------------------------------------
// Formatgruppen für die Optionen
// ---------------------------------------------------------------------------

const QSet<QString> htmlFormats = {"html", "html4", "html5", "chunkedhtml"};
const QSet<QString> htmlSlideFormats = {"revealjs", "slidy", "slideous", "dzslides", "s5"};
const QSet<QString> epubFormats = {"epub", "epub2", "epub3"};
const QSet<QString> markdownFormats = {"markdown", "markdown_strict", "markdown_phpextra",
                                       "markdown_mmd", "markdown_github", "gfm",
                                       "commonmark", "commonmark_x", "markua"};
// Formate, deren Ausgabe ohnehin immer ein vollständiges Dokument ist
const QSet<QString> alwaysStandalone = {"pdf", "docx", "odt", "pptx", "epub", "epub2",
                                        "epub3", "fb2", "ipynb", "beamer", "chunkedhtml"};
// Formate, für die eine Ausgabe als Fragment üblich ist
const QSet<QString> fragmentByDefault = {
    "markdown", "markdown_strict", "markdown_phpextra", "markdown_mmd", "markdown_github",
    "gfm", "commonmark", "commonmark_x", "markua", "djot", "rst", "asciidoc",
    "asciidoctor", "asciidoc_legacy", "org", "plain", "textile", "mediawiki", "dokuwiki",
    "zimwiki", "xwiki", "tikiwiki", "jira", "haddock", "muse", "json", "native", "ansi",
    "bbcode", "bbcode_phpbb", "bbcode_fluxbb", "bbcode_steam", "bbcode_hubzilla",
    "bbcode_xenforo", "vimdoc", "biblatex", "bibtex", "csljson", "xml"};
// Binäre bzw. gepackte Ausgaben ohne Zeilenumbruch-Einstellungen
const QSet<QString> binaryFormats = {"pdf", "docx", "odt", "pptx", "epub", "epub2", "epub3",
                                     "beamer", "chunkedhtml"};
const QSet<QString> highlightFormats = {"html", "html4", "html5", "chunkedhtml", "revealjs",
                                        "slidy", "slideous", "dzslides", "s5", "latex",
                                        "beamer", "pdf", "docx", "epub", "epub2", "epub3",
                                        "typst", "context", "ms"};

const QStringList highlightStyles = {"pygments", "tango", "espresso", "zenburn", "kate",
                                     "monochrome", "breezedark", "haddock"};
const QStringList mathMethods = {"mathjax", "katex", "mathml", "webtex", "gladtex"};

// Hilfsfunktionen zum kompakten Anlegen von Optionen

OptionDef boolOpt(const QString &flag, const QString &label, const QString &tip, bool def = false)
{
    OptionDef o;
    o.id = flag;
    o.label = label;
    o.toolTip = tip;
    o.type = OptionDef::Type::Bool;
    o.style = OptionDef::Style::Switch;
    o.flag = flag;
    o.defaultValue = def;
    return o;
}

OptionDef intOpt(const QString &flag, const QString &label, const QString &tip, int min, int max)
{
    OptionDef o;
    o.id = flag;
    o.label = label;
    o.toolTip = tip;
    o.type = OptionDef::Type::Int;
    o.style = OptionDef::Style::Value;
    o.flag = flag;
    o.defaultValue = 0;
    o.minimum = min;
    o.maximum = max;
    return o;
}

OptionDef choiceOpt(const QString &flag, const QString &label, const QString &tip,
                    const QStringList &choices,
                    OptionDef::Style style = OptionDef::Style::Value)
{
    OptionDef o;
    o.id = flag;
    o.label = label;
    o.toolTip = tip;
    o.type = OptionDef::Type::Choice;
    o.style = style;
    o.flag = flag;
    o.defaultValue = QString();
    o.choices = choices;
    return o;
}

OptionDef fileOpt(const QString &flag, const QString &label, const QString &tip,
                  const QString &filter)
{
    OptionDef o;
    o.id = flag;
    o.label = label;
    o.toolTip = tip;
    o.type = OptionDef::Type::File;
    o.style = OptionDef::Style::Value;
    o.flag = flag;
    o.defaultValue = QString();
    o.fileFilter = filter;
    return o;
}

OptionDef textOpt(const QString &flag, const QString &label, const QString &tip,
                  const QString &placeholder = {})
{
    OptionDef o;
    o.id = flag;
    o.label = label;
    o.toolTip = tip;
    o.type = OptionDef::Type::Text;
    o.style = OptionDef::Style::Value;
    o.flag = flag;
    o.defaultValue = QString();
    o.placeholder = placeholder;
    return o;
}

OptionDef variable(OptionDef o)
{
    o.style = OptionDef::Style::Variable;
    o.id = QStringLiteral("V:") + o.flag;
    return o;
}

OptionDef metadata(OptionDef o)
{
    o.style = OptionDef::Style::Metadata;
    o.id = QStringLiteral("M:") + o.flag;
    return o;
}

QList<OptionDef> latexVariables()
{
    return {
        variable(choiceOpt("documentclass", Tr::tr("Dokumentklasse"),
                           Tr::tr("LaTeX-Dokumentklasse (-V documentclass)"),
                           {"article", "report", "book", "scrartcl", "scrreprt", "scrbook"})),
        variable(choiceOpt("papersize", Tr::tr("Papierformat"), Tr::tr("Papiergröße (-V papersize)"),
                           {"a4", "a5", "letter", "legal"})),
        variable(choiceOpt("fontsize", Tr::tr("Schriftgröße"), Tr::tr("Grundschriftgröße (-V fontsize)"),
                           {"10pt", "11pt", "12pt"})),
        variable(textOpt("geometry", Tr::tr("Seitenränder"),
                         Tr::tr("Wert für das LaTeX-Paket geometry (-V geometry)"),
                         "margin=2.5cm")),
        variable(textOpt("mainfont", Tr::tr("Schriftart"),
                         Tr::tr("Hauptschrift; nur mit xelatex oder lualatex (-V mainfont)"),
                         Tr::tr("z. B. Liberation Serif"))),
        variable(textOpt("linestretch", Tr::tr("Zeilenabstand"),
                         Tr::tr("Faktor für den Zeilenabstand (-V linestretch)"), Tr::tr("z. B. 1.25"))),
        variable(boolOpt("colorlinks", Tr::tr("Farbige Links"),
                         Tr::tr("Hyperlinks farbig statt umrandet darstellen (-V colorlinks)"))),
        variable(boolOpt("lof", Tr::tr("Abbildungsverzeichnis"),
                         Tr::tr("Abbildungsverzeichnis einfügen (-V lof)"))),
        variable(boolOpt("lot", Tr::tr("Tabellenverzeichnis"),
                         Tr::tr("Tabellenverzeichnis einfügen (-V lot)"))),
    };
}

QList<OptionDef> slideOptions()
{
    return {
        intOpt("--slide-level", Tr::tr("Folienebene"),
               Tr::tr("Überschriftenebene, die eine neue Folie beginnt (--slide-level)"), 1, 6),
        boolOpt("--incremental", Tr::tr("Listen schrittweise einblenden"),
                Tr::tr("Listenpunkte nacheinander anzeigen (--incremental)")),
    };
}

QList<OptionDef> specificOptions(const QString &f)
{
    const QString cssFilter = Tr::tr("CSS-Dateien (*.css);;Alle Dateien (*)");

    if (htmlFormats.contains(f)) {
        QList<OptionDef> list = {
            boolOpt("--embed-resources", Tr::tr("Ressourcen einbetten"),
                    Tr::tr("Bilder, Stylesheets und Skripte in die HTML-Datei einbetten "
                       "(--embed-resources, benötigt „Vollständiges Dokument“)")),
            fileOpt("--css", Tr::tr("Stylesheet (CSS)"), Tr::tr("CSS-Datei verknüpfen (--css)"), cssFilter),
            choiceOpt("math", Tr::tr("Formeldarstellung"),
                      Tr::tr("Methode zur Darstellung mathematischer Formeln"), mathMethods,
                      OptionDef::Style::ChoiceSwitch),
            boolOpt("--section-divs", Tr::tr("Abschnitte in <section> einschließen"),
                    Tr::tr("Abschnitte als <section>-Elemente ausgeben (--section-divs)")),
        };
        if (f == QLatin1String("chunkedhtml"))
            list << intOpt("--split-level", Tr::tr("Aufteilungsebene"),
                           Tr::tr("Überschriftenebene, an der in Einzeldateien aufgeteilt wird "
                              "(--split-level)"), 1, 6);
        return list;
    }

    if (htmlSlideFormats.contains(f)) {
        QList<OptionDef> list = slideOptions();
        list << boolOpt("--embed-resources", Tr::tr("Ressourcen einbetten"),
                        Tr::tr("Alles in eine einzelne HTML-Datei einbetten (--embed-resources)"))
             << fileOpt("--css", Tr::tr("Stylesheet (CSS)"), Tr::tr("CSS-Datei verknüpfen (--css)"),
                        cssFilter)
             << choiceOpt("math", Tr::tr("Formeldarstellung"),
                          Tr::tr("Methode zur Darstellung mathematischer Formeln"), mathMethods,
                          OptionDef::Style::ChoiceSwitch);
        if (f == QLatin1String("revealjs")) {
            list << variable(choiceOpt("theme", Tr::tr("Design"), Tr::tr("reveal.js-Theme (-V theme)"),
                                       {"black", "white", "league", "beige", "sky", "night",
                                        "serif", "simple", "solarized", "blood", "moon",
                                        "dracula"}))
                 << variable(choiceOpt("transition", Tr::tr("Folienübergang"),
                                       Tr::tr("Übergangseffekt (-V transition)"),
                                       {"none", "fade", "slide", "convex", "concave", "zoom"}));
        }
        return list;
    }

    if (f == QLatin1String("pdf")) {
        QList<OptionDef> list = {
            choiceOpt("--pdf-engine", Tr::tr("PDF-Erzeugung"),
                      Tr::tr("Programm, mit dem pandoc das PDF erzeugt (--pdf-engine). "
                         "Das Programm muss installiert sein."),
                      {"pdflatex", "xelatex", "lualatex", "latexmk", "tectonic", "typst",
                       "context", "wkhtmltopdf", "weasyprint", "pagedjs-cli", "prince"}),
        };
        list << latexVariables();
        return list;
    }

    if (f == QLatin1String("latex")) {
        return latexVariables();
    }

    if (f == QLatin1String("beamer")) {
        QList<OptionDef> list = slideOptions();
        list << choiceOpt("--pdf-engine", Tr::tr("PDF-Erzeugung"),
                          Tr::tr("Programm, mit dem pandoc das PDF erzeugt (--pdf-engine)"),
                          {"pdflatex", "xelatex", "lualatex", "latexmk", "tectonic"})
             << variable(choiceOpt("theme", Tr::tr("Design"), Tr::tr("Beamer-Theme (-V theme)"),
                                   {"default", "Madrid", "Warsaw", "Berlin", "Boadilla",
                                    "CambridgeUS", "Copenhagen", "Darmstadt", "Frankfurt",
                                    "Singapore", "Szeged", "metropolis"}))
             << variable(choiceOpt("aspectratio", Tr::tr("Seitenverhältnis"),
                                   Tr::tr("Seitenverhältnis der Folien (-V aspectratio)"),
                                   {"169", "1610", "43", "149", "54"}));
        return list;
    }

    if (f == QLatin1String("context")) {
        return {variable(choiceOpt("papersize", Tr::tr("Papierformat"),
                                   Tr::tr("Papiergröße (-V papersize)"), {"A4", "A5", "letter"}))};
    }

    if (f == QLatin1String("docx")) {
        return {fileOpt("--reference-doc", Tr::tr("Vorlage (Referenzdokument)"),
                        Tr::tr("Word-Datei, deren Formatvorlagen übernommen werden (--reference-doc)"),
                        Tr::tr("Word-Dokumente (*.docx *.dotx);;Alle Dateien (*)"))};
    }

    if (f == QLatin1String("odt")) {
        return {fileOpt("--reference-doc", Tr::tr("Vorlage (Referenzdokument)"),
                        Tr::tr("ODT-Datei, deren Formatvorlagen übernommen werden (--reference-doc)"),
                        Tr::tr("OpenDocument-Text (*.odt *.ott);;Alle Dateien (*)"))};
    }

    if (f == QLatin1String("pptx")) {
        QList<OptionDef> list = {
            fileOpt("--reference-doc", Tr::tr("Vorlage (Referenzdokument)"),
                    Tr::tr("PowerPoint-Datei, deren Layouts übernommen werden (--reference-doc)"),
                    Tr::tr("PowerPoint (*.pptx *.potx);;Alle Dateien (*)")),
        };
        list << slideOptions();
        return list;
    }

    if (epubFormats.contains(f)) {
        return {
            fileOpt("--epub-cover-image", Tr::tr("Titelbild"), Tr::tr("Coverbild (--epub-cover-image)"),
                    Tr::tr("Bilder (*.png *.jpg *.jpeg *.gif *.svg);;Alle Dateien (*)")),
            fileOpt("--css", Tr::tr("Stylesheet (CSS)"), Tr::tr("CSS-Datei für das E-Book (--css)"),
                    cssFilter),
            fileOpt("--epub-metadata", Tr::tr("Metadaten (XML)"),
                    Tr::tr("Dublin-Core-Metadaten als XML-Datei (--epub-metadata)"),
                    Tr::tr("XML-Dateien (*.xml);;Alle Dateien (*)")),
            fileOpt("--epub-embed-font", Tr::tr("Schriftart einbetten"),
                    Tr::tr("Schriftdatei, die eingebettet wird (--epub-embed-font)"),
                    Tr::tr("Schriften (*.ttf *.otf *.woff *.woff2);;Alle Dateien (*)")),
            intOpt("--split-level", Tr::tr("Kapitelebene"),
                   Tr::tr("Überschriftenebene, an der in Kapiteldateien aufgeteilt wird "
                      "(--split-level)"), 1, 6),
        };
    }

    if (markdownFormats.contains(f)) {
        QList<OptionDef> list = {
            boolOpt("--reference-links", Tr::tr("Referenz-Links"),
                    Tr::tr("Links als Referenzen statt inline ausgeben (--reference-links)")),
        };
        if (f.startsWith(QLatin1String("markdown")))
            list << choiceOpt("--markdown-headings", Tr::tr("Überschriftenstil"),
                              Tr::tr("ATX (# Titel) oder Setext (unterstrichen) "
                                 "(--markdown-headings)"),
                              {"atx", "setext"});
        return list;
    }

    if (f == QLatin1String("rst")) {
        return {boolOpt("--reference-links", Tr::tr("Referenz-Links"),
                        Tr::tr("Links als Referenzen statt inline ausgeben (--reference-links)"))};
    }

    if (f == QLatin1String("typst")) {
        return {
            variable(choiceOpt("papersize", Tr::tr("Papierformat"), Tr::tr("Papiergröße (-V papersize)"),
                               {"a4", "a5", "us-letter"})),
            variable(textOpt("mainfont", Tr::tr("Schriftart"), Tr::tr("Hauptschrift (-V mainfont)"))),
            variable(textOpt("fontsize", Tr::tr("Schriftgröße"), Tr::tr("Schriftgröße (-V fontsize)"),
                             Tr::tr("z. B. 11pt"))),
        };
    }

    if (f == QLatin1String("ipynb")) {
        return {choiceOpt("--ipynb-output", Tr::tr("Zellausgaben"),
                          Tr::tr("Welche Zellausgaben übernommen werden (--ipynb-output)"),
                          {"best", "all", "none"})};
    }

    return {};
}

} // namespace

// ---------------------------------------------------------------------------

QString detectInputFormat(const QString &filePath, const QStringList &available, QString *reason)
{
    const QFileInfo fi(filePath);
    const QString suffix = fi.suffix().toLower();
    QString format;
    QString how;

    if (suffix == QLatin1String("xml")) {
        format = sniffXml(filePath);
        how = Tr::tr("anhand des Dateiinhalts");
    } else if (suffix == QLatin1String("json")) {
        format = sniffJson(filePath);
        how = Tr::tr("anhand des Dateiinhalts");
    } else {
        format = inputExtensionMap().value(suffix);
        how = Tr::tr("anhand der Dateiendung .%1").arg(suffix);
    }

    if (format.isEmpty() || (!available.isEmpty() && !available.contains(format))) {
        if (reason)
            reason->clear();
        return {};
    }
    if (reason)
        *reason = how;
    return format;
}

QString outputExtension(const QString &format)
{
    static const QHash<QString, QString> map = {
        {"html", ".html"},       {"html4", ".html"},       {"html5", ".html"},
        {"revealjs", ".html"},   {"slidy", ".html"},       {"slideous", ".html"},
        {"dzslides", ".html"},   {"s5", ".html"},          {"chunkedhtml", ".zip"},
        {"pdf", ".pdf"},         {"beamer", ".pdf"},       {"latex", ".tex"},
        {"context", ".tex"},     {"texinfo", ".texi"},     {"man", ".1"},
        {"ms", ".ms"},           {"docx", ".docx"},        {"odt", ".odt"},
        {"pptx", ".pptx"},       {"rtf", ".rtf"},          {"epub", ".epub"},
        {"epub2", ".epub"},      {"epub3", ".epub"},       {"fb2", ".fb2"},
        {"ipynb", ".ipynb"},     {"opml", ".opml"},        {"typst", ".typ"},
        {"rst", ".rst"},         {"org", ".org"},          {"textile", ".textile"},
        {"asciidoc", ".adoc"},   {"asciidoctor", ".adoc"}, {"asciidoc_legacy", ".adoc"},
        {"markdown", ".md"},     {"markdown_strict", ".md"}, {"markdown_phpextra", ".md"},
        {"markdown_mmd", ".md"}, {"markdown_github", ".md"}, {"gfm", ".md"},
        {"commonmark", ".md"},   {"commonmark_x", ".md"},  {"markua", ".md"},
        {"djot", ".dj"},         {"plain", ".txt"},        {"ansi", ".txt"},
        {"json", ".json"},       {"csljson", ".json"},     {"native", ".native"},
        {"docbook", ".xml"},     {"docbook4", ".xml"},     {"docbook5", ".xml"},
        {"jats", ".xml"},        {"jats_archiving", ".xml"}, {"jats_articleauthoring", ".xml"},
        {"jats_publishing", ".xml"}, {"tei", ".xml"},      {"xml", ".xml"},
        {"icml", ".icml"},       {"mediawiki", ".wiki"},   {"dokuwiki", ".txt"},
        {"zimwiki", ".txt"},     {"xwiki", ".txt"},        {"tikiwiki", ".txt"},
        {"jira", ".txt"},        {"haddock", ".txt"},      {"muse", ".muse"},
        {"biblatex", ".bib"},    {"bibtex", ".bib"},       {"vimdoc", ".txt"},
        {"bbcode", ".txt"},      {"bbcode_phpbb", ".txt"}, {"bbcode_fluxbb", ".txt"},
        {"bbcode_steam", ".txt"}, {"bbcode_hubzilla", ".txt"}, {"bbcode_xenforo", ".txt"},
    };
    return map.value(format, QLatin1Char('.') + format);
}

QString outputFileName(const QString &baseName, const QString &format,
                       const QStringList &selectedFormats)
{
    const QString ext = outputExtension(format);
    QStringList colliders;
    for (const QString &other : selectedFormats) {
        if (outputExtension(other) == ext)
            colliders << other;
    }
    if (colliders.size() <= 1 || !colliders.contains(format))
        return baseName + ext;

    // Das Format, dessen Name der Endung entspricht (z. B. "html" für .html),
    // behält den einfachen Namen, sonst das zuerst gewählte.
    QString owner;
    for (const QString &c : std::as_const(colliders)) {
        if (QLatin1Char('.') + c == ext) {
            owner = c;
            break;
        }
    }
    if (owner.isEmpty())
        owner = colliders.first();

    return format == owner ? baseName + ext : baseName + QLatin1Char('.') + format + ext;
}

QString description(const QString &format)
{
    static const QHash<QString, const char *> map = {
        {"pdf", QT_TRANSLATE_NOOP("PandocFormats", "PDF-Dokument")},
        {"html", QT_TRANSLATE_NOOP("PandocFormats", "HTML-Webseite")},
        {"html4", QT_TRANSLATE_NOOP("PandocFormats", "HTML 4 / XHTML")},
        {"html5", QT_TRANSLATE_NOOP("PandocFormats", "HTML5-Webseite")},
        {"chunkedhtml", QT_TRANSLATE_NOOP("PandocFormats", "HTML, in Abschnitte aufgeteilt (ZIP)")},
        {"docx", QT_TRANSLATE_NOOP("PandocFormats", "Microsoft Word")},
        {"odt", QT_TRANSLATE_NOOP("PandocFormats", "OpenDocument-Text (LibreOffice)")},
        {"rtf", QT_TRANSLATE_NOOP("PandocFormats", "Rich Text Format")},
        {"epub", QT_TRANSLATE_NOOP("PandocFormats", "E-Book (EPUB 3)")},
        {"epub2", QT_TRANSLATE_NOOP("PandocFormats", "E-Book (EPUB 2)")},
        {"epub3", QT_TRANSLATE_NOOP("PandocFormats", "E-Book (EPUB 3)")},
        {"fb2", QT_TRANSLATE_NOOP("PandocFormats", "E-Book (FictionBook 2)")},
        {"pptx", QT_TRANSLATE_NOOP("PandocFormats", "PowerPoint-Präsentation")},
        {"latex", QT_TRANSLATE_NOOP("PandocFormats", "LaTeX-Quelltext")},
        {"beamer", QT_TRANSLATE_NOOP("PandocFormats", "LaTeX-Beamer-Folien (PDF)")},
        {"context", QT_TRANSLATE_NOOP("PandocFormats", "ConTeXt-Quelltext")},
        {"typst", QT_TRANSLATE_NOOP("PandocFormats", "Typst-Quelltext")},
        {"texinfo", QT_TRANSLATE_NOOP("PandocFormats", "GNU Texinfo")},
        {"markdown", QT_TRANSLATE_NOOP("PandocFormats", "Pandoc-Markdown")},
        {"markdown_strict", QT_TRANSLATE_NOOP("PandocFormats", "Markdown (ursprüngliche Syntax)")},
        {"markdown_phpextra", QT_TRANSLATE_NOOP("PandocFormats", "PHP Markdown Extra")},
        {"markdown_mmd", QT_TRANSLATE_NOOP("PandocFormats", "MultiMarkdown")},
        {"markdown_github", QT_TRANSLATE_NOOP("PandocFormats", "GitHub-Markdown (veraltet)")},
        {"gfm", QT_TRANSLATE_NOOP("PandocFormats", "GitHub-Markdown")},
        {"commonmark", QT_TRANSLATE_NOOP("PandocFormats", "CommonMark")},
        {"commonmark_x", QT_TRANSLATE_NOOP("PandocFormats", "CommonMark mit Erweiterungen")},
        {"markua", QT_TRANSLATE_NOOP("PandocFormats", "Markua")},
        {"djot", QT_TRANSLATE_NOOP("PandocFormats", "Djot")},
        {"rst", QT_TRANSLATE_NOOP("PandocFormats", "reStructuredText")},
        {"asciidoc", QT_TRANSLATE_NOOP("PandocFormats", "AsciiDoc")},
        {"asciidoctor", QT_TRANSLATE_NOOP("PandocFormats", "AsciiDoc (Asciidoctor)")},
        {"asciidoc_legacy", QT_TRANSLATE_NOOP("PandocFormats", "AsciiDoc (Python-Version)")},
        {"org", QT_TRANSLATE_NOOP("PandocFormats", "Emacs Org-Mode")},
        {"plain", QT_TRANSLATE_NOOP("PandocFormats", "Reiner Text")},
        {"ansi", QT_TRANSLATE_NOOP("PandocFormats", "Text mit ANSI-Formatierung")},
        {"textile", QT_TRANSLATE_NOOP("PandocFormats", "Textile")},
        {"mediawiki", QT_TRANSLATE_NOOP("PandocFormats", "MediaWiki")},
        {"dokuwiki", QT_TRANSLATE_NOOP("PandocFormats", "DokuWiki")},
        {"zimwiki", QT_TRANSLATE_NOOP("PandocFormats", "Zim-Wiki")},
        {"xwiki", QT_TRANSLATE_NOOP("PandocFormats", "XWiki")},
        {"tikiwiki", QT_TRANSLATE_NOOP("PandocFormats", "TikiWiki")},
        {"jira", QT_TRANSLATE_NOOP("PandocFormats", "Jira-Wiki")},
        {"creole", QT_TRANSLATE_NOOP("PandocFormats", "Creole-Wiki")},
        {"twiki", QT_TRANSLATE_NOOP("PandocFormats", "TWiki")},
        {"vimwiki", QT_TRANSLATE_NOOP("PandocFormats", "Vimwiki")},
        {"revealjs", QT_TRANSLATE_NOOP("PandocFormats", "reveal.js-Präsentation (HTML)")},
        {"slidy", QT_TRANSLATE_NOOP("PandocFormats", "Slidy-Präsentation (HTML)")},
        {"slideous", QT_TRANSLATE_NOOP("PandocFormats", "Slideous-Präsentation (HTML)")},
        {"dzslides", QT_TRANSLATE_NOOP("PandocFormats", "DZSlides-Präsentation (HTML)")},
        {"s5", QT_TRANSLATE_NOOP("PandocFormats", "S5-Präsentation (HTML)")},
        {"man", QT_TRANSLATE_NOOP("PandocFormats", "Manpage (roff)")},
        {"ms", QT_TRANSLATE_NOOP("PandocFormats", "roff ms")},
        {"docbook", QT_TRANSLATE_NOOP("PandocFormats", "DocBook XML")},
        {"docbook4", QT_TRANSLATE_NOOP("PandocFormats", "DocBook 4")},
        {"docbook5", QT_TRANSLATE_NOOP("PandocFormats", "DocBook 5")},
        {"jats", QT_TRANSLATE_NOOP("PandocFormats", "JATS XML")},
        {"jats_archiving", QT_TRANSLATE_NOOP("PandocFormats", "JATS Archiving")},
        {"jats_articleauthoring", QT_TRANSLATE_NOOP("PandocFormats", "JATS Article Authoring")},
        {"jats_publishing", QT_TRANSLATE_NOOP("PandocFormats", "JATS Publishing")},
        {"tei", QT_TRANSLATE_NOOP("PandocFormats", "TEI Simple")},
        {"icml", QT_TRANSLATE_NOOP("PandocFormats", "Adobe InCopy (ICML)")},
        {"opml", QT_TRANSLATE_NOOP("PandocFormats", "OPML-Gliederung")},
        {"ipynb", QT_TRANSLATE_NOOP("PandocFormats", "Jupyter Notebook")},
        {"json", QT_TRANSLATE_NOOP("PandocFormats", "Pandoc-AST als JSON")},
        {"native", QT_TRANSLATE_NOOP("PandocFormats", "Pandoc-AST (Haskell)")},
        {"xml", QT_TRANSLATE_NOOP("PandocFormats", "Pandoc-AST als XML")},
        {"haddock", QT_TRANSLATE_NOOP("PandocFormats", "Haddock-Markup")},
        {"muse", QT_TRANSLATE_NOOP("PandocFormats", "Emacs Muse")},
        {"biblatex", QT_TRANSLATE_NOOP("PandocFormats", "BibLaTeX-Literaturliste")},
        {"bibtex", QT_TRANSLATE_NOOP("PandocFormats", "BibTeX-Literaturliste")},
        {"csljson", QT_TRANSLATE_NOOP("PandocFormats", "CSL-JSON-Literaturliste")},
        {"endnotexml", QT_TRANSLATE_NOOP("PandocFormats", "EndNote XML")},
        {"ris", QT_TRANSLATE_NOOP("PandocFormats", "RIS-Literaturliste")},
        {"csv", QT_TRANSLATE_NOOP("PandocFormats", "CSV-Tabelle")},
        {"tsv", QT_TRANSLATE_NOOP("PandocFormats", "TSV-Tabelle")},
        {"xlsx", QT_TRANSLATE_NOOP("PandocFormats", "Excel-Tabelle")},
        {"t2t", QT_TRANSLATE_NOOP("PandocFormats", "txt2tags")},
        {"pod", QT_TRANSLATE_NOOP("PandocFormats", "Perl POD")},
        {"vimdoc", QT_TRANSLATE_NOOP("PandocFormats", "Vim-Hilfedatei")},
        {"bbcode", QT_TRANSLATE_NOOP("PandocFormats", "BBCode")},
    };
    const auto it = map.constFind(format);
    return it == map.constEnd() ? QString() : Tr::tr(it.value());
}

QStringList commonOutputFormats()
{
    return {"pdf", "html", "docx", "odt", "epub", "pptx", "latex", "markdown", "gfm",
            "rst", "typst", "revealjs", "beamer", "plain"};
}

QStringList fallbackInputFormats()
{
    return {"biblatex", "bibtex", "commonmark", "commonmark_x", "creole", "csljson", "csv",
            "docbook", "docx", "dokuwiki", "endnotexml", "epub", "fb2", "gfm", "haddock",
            "html", "ipynb", "jats", "jira", "json", "latex", "man", "markdown",
            "markdown_mmd", "markdown_phpextra", "markdown_strict", "mediawiki", "muse",
            "native", "odt", "opml", "org", "ris", "rst", "rtf", "t2t", "textile",
            "tikiwiki", "tsv", "twiki", "typst", "vimwiki"};
}

QStringList fallbackOutputFormats()
{
    return {"asciidoc", "beamer", "commonmark", "commonmark_x", "context", "docbook",
            "docx", "dokuwiki", "epub", "epub2", "fb2", "gfm", "haddock", "html", "html4",
            "icml", "ipynb", "jats", "jira", "json", "latex", "man", "markdown",
            "markdown_mmd", "markdown_phpextra", "markdown_strict", "mediawiki", "ms",
            "muse", "native", "odt", "opml", "org", "plain", "pptx", "revealjs", "rst",
            "rtf", "s5", "slideous", "slidy", "tei", "texinfo", "textile", "typst",
            "xwiki", "zimwiki"};
}

QList<OptionGroup> optionGroups(const QString &f)
{
    QList<OptionGroup> groups;

    OptionGroup general{Tr::tr("Allgemein"), {}};
    if (!alwaysStandalone.contains(f)) {
        general.options << boolOpt("--standalone", Tr::tr("Vollständiges Dokument"),
                                   Tr::tr("Mit Kopf- und Fußteil statt als Fragment ausgeben "
                                      "(--standalone)"),
                                   !fragmentByDefault.contains(f));
    }
    general.options << boolOpt("--toc", Tr::tr("Inhaltsverzeichnis"),
                               Tr::tr("Automatisches Inhaltsverzeichnis erzeugen (--toc)"))
                    << intOpt("--toc-depth", Tr::tr("Tiefe des Inhaltsverzeichnisses"),
                              Tr::tr("Anzahl der Überschriftenebenen im Verzeichnis (--toc-depth)"),
                              1, 6)
                    << boolOpt("--number-sections", Tr::tr("Abschnitte nummerieren"),
                               Tr::tr("Überschriften nummerieren (--number-sections)"));
    groups << general;

    groups << OptionGroup{
        Tr::tr("Metadaten"),
        {
            metadata(textOpt("title", Tr::tr("Titel"),
                             Tr::tr("Dokumenttitel; überschreibt den Titel der Eingabe (-M title)"))),
            metadata(textOpt("author", Tr::tr("Autor"), Tr::tr("Autor des Dokuments (-M author)"))),
            metadata(textOpt("lang", Tr::tr("Sprache"),
                             Tr::tr("Sprache des Dokuments als BCP-47-Code (-M lang)"), "de-DE")),
        }};

    const QList<OptionDef> specific = specificOptions(f);
    if (!specific.isEmpty()) {
        const QString desc = description(f);
        groups << OptionGroup{Tr::tr("Formatspezifisch: %1").arg(desc.isEmpty() ? f : desc),
                              specific};
    }

    OptionGroup advanced{Tr::tr("Erweitert"), {}};
    if (f != QLatin1String("docx") && f != QLatin1String("pptx")) {
        advanced.options << fileOpt("--template", Tr::tr("Vorlage (Template)"),
                                    Tr::tr("Eigene pandoc-Vorlage verwenden (--template)"),
                                    Tr::tr("Alle Dateien (*)"));
    }
    if (highlightFormats.contains(f)) {
        advanced.options << choiceOpt("--highlight-style", Tr::tr("Syntaxhervorhebung"),
                                      Tr::tr("Farbschema für Quelltextblöcke (--highlight-style)"),
                                      highlightStyles);
    }
    if (!binaryFormats.contains(f)) {
        advanced.options << choiceOpt("--wrap", Tr::tr("Zeilenumbruch"),
                                      Tr::tr("Umbruch langer Zeilen in der Ausgabe (--wrap)"),
                                      {"auto", "none", "preserve"})
                         << intOpt("--columns", Tr::tr("Zeilenbreite"),
                                   Tr::tr("Zeichen pro Zeile beim Umbruch (--columns)"), 10, 1000);
    }
    advanced.options << boolOpt("--citeproc", Tr::tr("Literaturangaben verarbeiten"),
                                Tr::tr("Zitate und Literaturverzeichnis erzeugen (--citeproc)"))
                     << fileOpt("--bibliography", Tr::tr("Literaturdatenbank"),
                                Tr::tr("Literaturdatei für --citeproc (--bibliography)"),
                                Tr::tr("Literatur (*.bib *.json *.yaml *.ris);;Alle Dateien (*)"))
                     << fileOpt("--csl", Tr::tr("Zitierstil (CSL)"),
                                Tr::tr("Zitierstil-Datei (--csl)"),
                                Tr::tr("CSL-Stile (*.csl);;Alle Dateien (*)"))
                     << fileOpt("--lua-filter", Tr::tr("Lua-Filter"),
                                Tr::tr("Lua-Filter anwenden (--lua-filter)"),
                                Tr::tr("Lua-Skripte (*.lua);;Alle Dateien (*)"));
    groups << advanced;

    return groups;
}

QStringList optionArguments(const QString &outputFormat, const QVariantMap &values)
{
    QStringList args;
    const QList<OptionGroup> groups = optionGroups(outputFormat);

    for (const OptionGroup &group : groups) {
        for (const OptionDef &opt : group.options) {
            const QVariant value = values.value(opt.id, opt.defaultValue);
            QString text;

            switch (opt.type) {
            case OptionDef::Type::Bool:
                if (!value.toBool())
                    continue;
                text = QStringLiteral("true");
                break;
            case OptionDef::Type::Int:
                if (value.toInt() <= 0)
                    continue;
                text = QString::number(value.toInt());
                break;
            case OptionDef::Type::Choice:
            case OptionDef::Type::Text:
            case OptionDef::Type::File:
                text = value.toString().trimmed();
                if (text.isEmpty())
                    continue;
                break;
            }

            switch (opt.style) {
            case OptionDef::Style::Switch:
                args << opt.flag;
                break;
            case OptionDef::Style::Value:
                args << opt.flag + QLatin1Char('=') + text;
                break;
            case OptionDef::Style::ChoiceSwitch:
                args << QStringLiteral("--") + text;
                break;
            case OptionDef::Style::Variable:
                args << QStringLiteral("-V") << opt.flag + QLatin1Char('=') + text;
                break;
            case OptionDef::Style::Metadata:
                args << QStringLiteral("-M") << opt.flag + QLatin1Char('=') + text;
                break;
            }
        }
    }

    const QString extra = values.value(QLatin1String(extraArgumentsKey())).toString().trimmed();
    if (!extra.isEmpty())
        args << QProcess::splitCommand(extra);

    return args;
}

QVariantMap nonDefaultValues(const QString &outputFormat, const QVariantMap &values)
{
    QVariantMap result;
    const QList<OptionGroup> groups = optionGroups(outputFormat);
    for (const OptionGroup &group : groups) {
        for (const OptionDef &opt : group.options) {
            if (!values.contains(opt.id))
                continue;
            const QVariant value = values.value(opt.id);
            bool isDefault = false;
            switch (opt.type) {
            case OptionDef::Type::Bool:
                isDefault = value.toBool() == opt.defaultValue.toBool();
                break;
            case OptionDef::Type::Int:
                isDefault = value.toInt() == opt.defaultValue.toInt();
                break;
            case OptionDef::Type::Choice:
            case OptionDef::Type::Text:
            case OptionDef::Type::File:
                isDefault = value.toString().trimmed() == opt.defaultValue.toString();
                break;
            }
            if (!isDefault)
                result.insert(opt.id, value);
        }
    }
    const QString extraKey = QLatin1String(extraArgumentsKey());
    if (!values.value(extraKey).toString().trimmed().isEmpty())
        result.insert(extraKey, values.value(extraKey));
    return result;
}

} // namespace PandocFormats
