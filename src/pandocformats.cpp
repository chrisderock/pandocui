#include "pandocformats.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

namespace PandocFormats {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("PandocFormats", text);
}

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
        variable(choiceOpt("documentclass", tr("Dokumentklasse"),
                           tr("LaTeX-Dokumentklasse (-V documentclass)"),
                           {"article", "report", "book", "scrartcl", "scrreprt", "scrbook"})),
        variable(choiceOpt("papersize", tr("Papierformat"), tr("Papiergröße (-V papersize)"),
                           {"a4", "a5", "letter", "legal"})),
        variable(choiceOpt("fontsize", tr("Schriftgröße"), tr("Grundschriftgröße (-V fontsize)"),
                           {"10pt", "11pt", "12pt"})),
        variable(textOpt("geometry", tr("Seitenränder"),
                         tr("Wert für das LaTeX-Paket geometry (-V geometry)"),
                         "margin=2.5cm")),
        variable(textOpt("mainfont", tr("Schriftart"),
                         tr("Hauptschrift; nur mit xelatex oder lualatex (-V mainfont)"),
                         "z. B. Liberation Serif")),
        variable(textOpt("linestretch", tr("Zeilenabstand"),
                         tr("Faktor für den Zeilenabstand (-V linestretch)"), "z. B. 1.25")),
        variable(boolOpt("colorlinks", tr("Farbige Links"),
                         tr("Hyperlinks farbig statt umrandet darstellen (-V colorlinks)"))),
        variable(boolOpt("lof", tr("Abbildungsverzeichnis"),
                         tr("Abbildungsverzeichnis einfügen (-V lof)"))),
        variable(boolOpt("lot", tr("Tabellenverzeichnis"),
                         tr("Tabellenverzeichnis einfügen (-V lot)"))),
    };
}

QList<OptionDef> slideOptions()
{
    return {
        intOpt("--slide-level", tr("Folienebene"),
               tr("Überschriftenebene, die eine neue Folie beginnt (--slide-level)"), 1, 6),
        boolOpt("--incremental", tr("Listen schrittweise einblenden"),
                tr("Listenpunkte nacheinander anzeigen (--incremental)")),
    };
}

QList<OptionDef> specificOptions(const QString &f)
{
    const QString cssFilter = tr("CSS-Dateien (*.css);;Alle Dateien (*)");

    if (htmlFormats.contains(f)) {
        QList<OptionDef> list = {
            boolOpt("--embed-resources", tr("Ressourcen einbetten"),
                    tr("Bilder, Stylesheets und Skripte in die HTML-Datei einbetten "
                       "(--embed-resources, benötigt „Vollständiges Dokument“)")),
            fileOpt("--css", tr("Stylesheet (CSS)"), tr("CSS-Datei verknüpfen (--css)"), cssFilter),
            choiceOpt("math", tr("Formeldarstellung"),
                      tr("Methode zur Darstellung mathematischer Formeln"), mathMethods,
                      OptionDef::Style::ChoiceSwitch),
            boolOpt("--section-divs", tr("Abschnitte in <section> einschließen"),
                    tr("Abschnitte als <section>-Elemente ausgeben (--section-divs)")),
        };
        if (f == QLatin1String("chunkedhtml"))
            list << intOpt("--split-level", tr("Aufteilungsebene"),
                           tr("Überschriftenebene, an der in Einzeldateien aufgeteilt wird "
                              "(--split-level)"), 1, 6);
        return list;
    }

    if (htmlSlideFormats.contains(f)) {
        QList<OptionDef> list = slideOptions();
        list << boolOpt("--embed-resources", tr("Ressourcen einbetten"),
                        tr("Alles in eine einzelne HTML-Datei einbetten (--embed-resources)"))
             << fileOpt("--css", tr("Stylesheet (CSS)"), tr("CSS-Datei verknüpfen (--css)"),
                        cssFilter)
             << choiceOpt("math", tr("Formeldarstellung"),
                          tr("Methode zur Darstellung mathematischer Formeln"), mathMethods,
                          OptionDef::Style::ChoiceSwitch);
        if (f == QLatin1String("revealjs")) {
            list << variable(choiceOpt("theme", tr("Design"), tr("reveal.js-Theme (-V theme)"),
                                       {"black", "white", "league", "beige", "sky", "night",
                                        "serif", "simple", "solarized", "blood", "moon",
                                        "dracula"}))
                 << variable(choiceOpt("transition", tr("Folienübergang"),
                                       tr("Übergangseffekt (-V transition)"),
                                       {"none", "fade", "slide", "convex", "concave", "zoom"}));
        }
        return list;
    }

    if (f == QLatin1String("pdf")) {
        QList<OptionDef> list = {
            choiceOpt("--pdf-engine", tr("PDF-Erzeugung"),
                      tr("Programm, mit dem pandoc das PDF erzeugt (--pdf-engine). "
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
        list << choiceOpt("--pdf-engine", tr("PDF-Erzeugung"),
                          tr("Programm, mit dem pandoc das PDF erzeugt (--pdf-engine)"),
                          {"pdflatex", "xelatex", "lualatex", "latexmk", "tectonic"})
             << variable(choiceOpt("theme", tr("Design"), tr("Beamer-Theme (-V theme)"),
                                   {"default", "Madrid", "Warsaw", "Berlin", "Boadilla",
                                    "CambridgeUS", "Copenhagen", "Darmstadt", "Frankfurt",
                                    "Singapore", "Szeged", "metropolis"}))
             << variable(choiceOpt("aspectratio", tr("Seitenverhältnis"),
                                   tr("Seitenverhältnis der Folien (-V aspectratio)"),
                                   {"169", "1610", "43", "149", "54"}));
        return list;
    }

    if (f == QLatin1String("context")) {
        return {variable(choiceOpt("papersize", tr("Papierformat"),
                                   tr("Papiergröße (-V papersize)"), {"A4", "A5", "letter"}))};
    }

    if (f == QLatin1String("docx")) {
        return {fileOpt("--reference-doc", tr("Vorlage (Referenzdokument)"),
                        tr("Word-Datei, deren Formatvorlagen übernommen werden (--reference-doc)"),
                        tr("Word-Dokumente (*.docx *.dotx);;Alle Dateien (*)"))};
    }

    if (f == QLatin1String("odt")) {
        return {fileOpt("--reference-doc", tr("Vorlage (Referenzdokument)"),
                        tr("ODT-Datei, deren Formatvorlagen übernommen werden (--reference-doc)"),
                        tr("OpenDocument-Text (*.odt *.ott);;Alle Dateien (*)"))};
    }

    if (f == QLatin1String("pptx")) {
        QList<OptionDef> list = {
            fileOpt("--reference-doc", tr("Vorlage (Referenzdokument)"),
                    tr("PowerPoint-Datei, deren Layouts übernommen werden (--reference-doc)"),
                    tr("PowerPoint (*.pptx *.potx);;Alle Dateien (*)")),
        };
        list << slideOptions();
        return list;
    }

    if (epubFormats.contains(f)) {
        return {
            fileOpt("--epub-cover-image", tr("Titelbild"), tr("Coverbild (--epub-cover-image)"),
                    tr("Bilder (*.png *.jpg *.jpeg *.gif *.svg);;Alle Dateien (*)")),
            fileOpt("--css", tr("Stylesheet (CSS)"), tr("CSS-Datei für das E-Book (--css)"),
                    cssFilter),
            fileOpt("--epub-metadata", tr("Metadaten (XML)"),
                    tr("Dublin-Core-Metadaten als XML-Datei (--epub-metadata)"),
                    tr("XML-Dateien (*.xml);;Alle Dateien (*)")),
            fileOpt("--epub-embed-font", tr("Schriftart einbetten"),
                    tr("Schriftdatei, die eingebettet wird (--epub-embed-font)"),
                    tr("Schriften (*.ttf *.otf *.woff *.woff2);;Alle Dateien (*)")),
            intOpt("--split-level", tr("Kapitelebene"),
                   tr("Überschriftenebene, an der in Kapiteldateien aufgeteilt wird "
                      "(--split-level)"), 1, 6),
        };
    }

    if (markdownFormats.contains(f)) {
        QList<OptionDef> list = {
            boolOpt("--reference-links", tr("Referenz-Links"),
                    tr("Links als Referenzen statt inline ausgeben (--reference-links)")),
        };
        if (f.startsWith(QLatin1String("markdown")))
            list << choiceOpt("--markdown-headings", tr("Überschriftenstil"),
                              tr("ATX (# Titel) oder Setext (unterstrichen) "
                                 "(--markdown-headings)"),
                              {"atx", "setext"});
        return list;
    }

    if (f == QLatin1String("rst")) {
        return {boolOpt("--reference-links", tr("Referenz-Links"),
                        tr("Links als Referenzen statt inline ausgeben (--reference-links)"))};
    }

    if (f == QLatin1String("typst")) {
        return {
            variable(choiceOpt("papersize", tr("Papierformat"), tr("Papiergröße (-V papersize)"),
                               {"a4", "a5", "us-letter"})),
            variable(textOpt("mainfont", tr("Schriftart"), tr("Hauptschrift (-V mainfont)"))),
            variable(textOpt("fontsize", tr("Schriftgröße"), tr("Schriftgröße (-V fontsize)"),
                             "z. B. 11pt")),
        };
    }

    if (f == QLatin1String("ipynb")) {
        return {choiceOpt("--ipynb-output", tr("Zellausgaben"),
                          tr("Welche Zellausgaben übernommen werden (--ipynb-output)"),
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
        how = tr("anhand des Dateiinhalts");
    } else if (suffix == QLatin1String("json")) {
        format = sniffJson(filePath);
        how = tr("anhand des Dateiinhalts");
    } else {
        format = inputExtensionMap().value(suffix);
        how = tr("anhand der Dateiendung .%1").arg(suffix);
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
        {"pdf", "PDF-Dokument"},
        {"html", "HTML-Webseite"},
        {"html4", "HTML 4 / XHTML"},
        {"html5", "HTML5-Webseite"},
        {"chunkedhtml", "HTML, in Abschnitte aufgeteilt (ZIP)"},
        {"docx", "Microsoft Word"},
        {"odt", "OpenDocument-Text (LibreOffice)"},
        {"rtf", "Rich Text Format"},
        {"epub", "E-Book (EPUB 3)"},
        {"epub2", "E-Book (EPUB 2)"},
        {"epub3", "E-Book (EPUB 3)"},
        {"fb2", "E-Book (FictionBook 2)"},
        {"pptx", "PowerPoint-Präsentation"},
        {"latex", "LaTeX-Quelltext"},
        {"beamer", "LaTeX-Beamer-Folien (PDF)"},
        {"context", "ConTeXt-Quelltext"},
        {"typst", "Typst-Quelltext"},
        {"texinfo", "GNU Texinfo"},
        {"markdown", "Pandoc-Markdown"},
        {"markdown_strict", "Markdown (ursprüngliche Syntax)"},
        {"markdown_phpextra", "PHP Markdown Extra"},
        {"markdown_mmd", "MultiMarkdown"},
        {"markdown_github", "GitHub-Markdown (veraltet)"},
        {"gfm", "GitHub-Markdown"},
        {"commonmark", "CommonMark"},
        {"commonmark_x", "CommonMark mit Erweiterungen"},
        {"markua", "Markua"},
        {"djot", "Djot"},
        {"rst", "reStructuredText"},
        {"asciidoc", "AsciiDoc"},
        {"asciidoctor", "AsciiDoc (Asciidoctor)"},
        {"asciidoc_legacy", "AsciiDoc (Python-Version)"},
        {"org", "Emacs Org-Mode"},
        {"plain", "Reiner Text"},
        {"ansi", "Text mit ANSI-Formatierung"},
        {"textile", "Textile"},
        {"mediawiki", "MediaWiki"},
        {"dokuwiki", "DokuWiki"},
        {"zimwiki", "Zim-Wiki"},
        {"xwiki", "XWiki"},
        {"tikiwiki", "TikiWiki"},
        {"jira", "Jira-Wiki"},
        {"creole", "Creole-Wiki"},
        {"twiki", "TWiki"},
        {"vimwiki", "Vimwiki"},
        {"revealjs", "reveal.js-Präsentation (HTML)"},
        {"slidy", "Slidy-Präsentation (HTML)"},
        {"slideous", "Slideous-Präsentation (HTML)"},
        {"dzslides", "DZSlides-Präsentation (HTML)"},
        {"s5", "S5-Präsentation (HTML)"},
        {"man", "Manpage (roff)"},
        {"ms", "roff ms"},
        {"docbook", "DocBook XML"},
        {"docbook4", "DocBook 4"},
        {"docbook5", "DocBook 5"},
        {"jats", "JATS XML"},
        {"jats_archiving", "JATS Archiving"},
        {"jats_articleauthoring", "JATS Article Authoring"},
        {"jats_publishing", "JATS Publishing"},
        {"tei", "TEI Simple"},
        {"icml", "Adobe InCopy (ICML)"},
        {"opml", "OPML-Gliederung"},
        {"ipynb", "Jupyter Notebook"},
        {"json", "Pandoc-AST als JSON"},
        {"native", "Pandoc-AST (Haskell)"},
        {"xml", "Pandoc-AST als XML"},
        {"haddock", "Haddock-Markup"},
        {"muse", "Emacs Muse"},
        {"biblatex", "BibLaTeX-Literaturliste"},
        {"bibtex", "BibTeX-Literaturliste"},
        {"csljson", "CSL-JSON-Literaturliste"},
        {"endnotexml", "EndNote XML"},
        {"ris", "RIS-Literaturliste"},
        {"csv", "CSV-Tabelle"},
        {"tsv", "TSV-Tabelle"},
        {"xlsx", "Excel-Tabelle"},
        {"t2t", "txt2tags"},
        {"pod", "Perl POD"},
        {"vimdoc", "Vim-Hilfedatei"},
        {"bbcode", "BBCode"},
    };
    const auto it = map.constFind(format);
    return it == map.constEnd() ? QString() : tr(it.value());
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

    OptionGroup general{tr("Allgemein"), {}};
    if (!alwaysStandalone.contains(f)) {
        general.options << boolOpt("--standalone", tr("Vollständiges Dokument"),
                                   tr("Mit Kopf- und Fußteil statt als Fragment ausgeben "
                                      "(--standalone)"),
                                   !fragmentByDefault.contains(f));
    }
    general.options << boolOpt("--toc", tr("Inhaltsverzeichnis"),
                               tr("Automatisches Inhaltsverzeichnis erzeugen (--toc)"))
                    << intOpt("--toc-depth", tr("Tiefe des Inhaltsverzeichnisses"),
                              tr("Anzahl der Überschriftenebenen im Verzeichnis (--toc-depth)"),
                              1, 6)
                    << boolOpt("--number-sections", tr("Abschnitte nummerieren"),
                               tr("Überschriften nummerieren (--number-sections)"));
    groups << general;

    groups << OptionGroup{
        tr("Metadaten"),
        {
            metadata(textOpt("title", tr("Titel"),
                             tr("Dokumenttitel; überschreibt den Titel der Eingabe (-M title)"))),
            metadata(textOpt("author", tr("Autor"), tr("Autor des Dokuments (-M author)"))),
            metadata(textOpt("lang", tr("Sprache"),
                             tr("Sprache des Dokuments als BCP-47-Code (-M lang)"), "de-DE")),
        }};

    const QList<OptionDef> specific = specificOptions(f);
    if (!specific.isEmpty()) {
        const QString desc = description(f);
        groups << OptionGroup{tr("Formatspezifisch: %1").arg(desc.isEmpty() ? f : desc),
                              specific};
    }

    OptionGroup advanced{tr("Erweitert"), {}};
    if (f != QLatin1String("docx") && f != QLatin1String("pptx")) {
        advanced.options << fileOpt("--template", tr("Vorlage (Template)"),
                                    tr("Eigene pandoc-Vorlage verwenden (--template)"),
                                    tr("Alle Dateien (*)"));
    }
    if (highlightFormats.contains(f)) {
        advanced.options << choiceOpt("--highlight-style", tr("Syntaxhervorhebung"),
                                      tr("Farbschema für Quelltextblöcke (--highlight-style)"),
                                      highlightStyles);
    }
    if (!binaryFormats.contains(f)) {
        advanced.options << choiceOpt("--wrap", tr("Zeilenumbruch"),
                                      tr("Umbruch langer Zeilen in der Ausgabe (--wrap)"),
                                      {"auto", "none", "preserve"})
                         << intOpt("--columns", tr("Zeilenbreite"),
                                   tr("Zeichen pro Zeile beim Umbruch (--columns)"), 10, 1000);
    }
    advanced.options << boolOpt("--citeproc", tr("Literaturangaben verarbeiten"),
                                tr("Zitate und Literaturverzeichnis erzeugen (--citeproc)"))
                     << fileOpt("--bibliography", tr("Literaturdatenbank"),
                                tr("Literaturdatei für --citeproc (--bibliography)"),
                                tr("Literatur (*.bib *.json *.yaml *.ris);;Alle Dateien (*)"))
                     << fileOpt("--csl", tr("Zitierstil (CSL)"),
                                tr("Zitierstil-Datei (--csl)"),
                                tr("CSL-Stile (*.csl);;Alle Dateien (*)"))
                     << fileOpt("--lua-filter", tr("Lua-Filter"),
                                tr("Lua-Filter anwenden (--lua-filter)"),
                                tr("Lua-Skripte (*.lua);;Alle Dateien (*)"));
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
