#include "conversionrunner.h"
#include "formatoptionswidget.h"
#include "helpdialog.h"
#include "mainwindow.h"
#include "pandocformats.h"
#include "pandocinfo.h"
#include "pandocmissingdialog.h"
#include "presetstore.h"
#include "translation.h"

#include <QCheckBox>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextDocument>
#include <QPushButton>
#include <QTreeWidget>
#include <QtTest>

using namespace PandocFormats;

class TestPandocUi : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void detectByExtension_data();
    void detectByExtension();
    void detectByContent();
    void detectRespectsAvailableFormats();

    void outputExtensions();
    void outputNamesAvoidCollisions();

    void defaultArguments();
    void optionArguments();
    void optionsWidgetRoundTrip();

    void mainWindowBuildsJobs();
    void addPdfToExistingSelection();
    void wizardNavigation();
    void presetStoreRoundTrip();
    void presetJsonErrors();
    void mainWindowPresets();

    void installHints_data();
    void installHints();
    void pandocMissingDialog();

    void languageResolution();
    void englishTranslation();

    void helpAnchors();
    void helpDialog_data();
    void helpDialog();
    void helpFromMainWindow();

    void endToEndConversion();

private:
    QString writeFile(const QString &name, const QByteArray &content);
    QTemporaryDir m_tmp;
    QString m_sample;
};

void TestPandocUi::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("PandocUiTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PandocUiTest"));
    QSettings().clear();
    QStandardPaths::setTestModeEnabled(true);
    QDir(PresetStore::defaultDirectory()).removeRecursively();
    QVERIFY(m_tmp.isValid());
    m_sample = QStringLiteral(PANDOCUI_TEST_DATA "/beispiel.md");
    QVERIFY(QFile::exists(m_sample));
}

QString TestPandocUi::writeFile(const QString &name, const QByteArray &content)
{
    const QString path = m_tmp.filePath(name);
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(content);
    return path;
}

void TestPandocUi::detectByExtension_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<QString>("format");
    QTest::newRow("md") << "a.md" << "markdown";
    QTest::newRow("upper") << "A.MD" << "markdown";
    QTest::newRow("docx") << "bericht.docx" << "docx";
    QTest::newRow("html") << "x.htm" << "html";
    QTest::newRow("tex") << "x.tex" << "latex";
    QTest::newRow("rst") << "x.rst" << "rst";
    QTest::newRow("typ") << "x.typ" << "typst";
    QTest::newRow("man") << "ls.1" << "man";
    QTest::newRow("multi-dot") << "my.notes.org" << "org";
    QTest::newRow("unknown") << "x.xyz" << "";
}

void TestPandocUi::detectByExtension()
{
    QFETCH(QString, file);
    QFETCH(QString, format);
    QString reason;
    QCOMPARE(detectInputFormat(file, {}, &reason), format);
    QCOMPARE(reason.isEmpty(), format.isEmpty());
}

void TestPandocUi::detectByContent()
{
    QCOMPARE(detectInputFormat(writeFile("nb.json", "{\"cells\": [], \"nbformat\": 4}")),
             QStringLiteral("ipynb"));
    QCOMPARE(detectInputFormat(writeFile("ast.json", "{\"pandoc-api-version\":[1,23]}")),
             QStringLiteral("json"));
    QCOMPARE(detectInputFormat(writeFile("refs.json", "[{\"id\":\"x\"}]")),
             QStringLiteral("csljson"));
    QCOMPARE(detectInputFormat(writeFile("o.xml", "<?xml version=\"1.0\"?><opml/>")),
             QStringLiteral("opml"));
    QCOMPARE(detectInputFormat(writeFile("d.xml", "<?xml version=\"1.0\"?><article/>")),
             QStringLiteral("docbook"));
}

void TestPandocUi::detectRespectsAvailableFormats()
{
    QCOMPARE(detectInputFormat("x.adoc", {"markdown", "html"}), QString());
    QCOMPARE(detectInputFormat("x.adoc", {"asciidoc"}), QStringLiteral("asciidoc"));
}

void TestPandocUi::outputExtensions()
{
    QCOMPARE(outputExtension("html5"), QStringLiteral(".html"));
    QCOMPARE(outputExtension("pdf"), QStringLiteral(".pdf"));
    QCOMPARE(outputExtension("gfm"), QStringLiteral(".md"));
    QCOMPARE(outputExtension("latex"), QStringLiteral(".tex"));
    QCOMPARE(outputExtension("epub3"), QStringLiteral(".epub"));
    QCOMPARE(outputExtension("unbekannt"), QStringLiteral(".unbekannt"));
}

void TestPandocUi::outputNamesAvoidCollisions()
{
    const QStringList sel = {"revealjs", "html", "docx", "gfm", "markdown"};
    QCOMPARE(outputFileName("doc", "html", sel), QStringLiteral("doc.html"));
    QCOMPARE(outputFileName("doc", "revealjs", sel), QStringLiteral("doc.revealjs.html"));
    QCOMPARE(outputFileName("doc", "docx", sel), QStringLiteral("doc.docx"));
    QCOMPARE(outputFileName("doc", "gfm", sel), QStringLiteral("doc.md"));
    QCOMPARE(outputFileName("doc", "markdown", sel), QStringLiteral("doc.markdown.md"));
}

void TestPandocUi::defaultArguments()
{
    // HTML ist standardmäßig ein vollständiges Dokument, Markdown ein Fragment
    QCOMPARE(PandocFormats::optionArguments("html", {}), QStringList{"--standalone"});
    QCOMPARE(PandocFormats::optionArguments("gfm", {}), QStringList{});
    QCOMPARE(PandocFormats::optionArguments("docx", {}), QStringList{});
}

void TestPandocUi::optionArguments()
{
    QVariantMap v;
    v.insert("--toc", true);
    v.insert("--toc-depth", 2);
    v.insert("--pdf-engine", "xelatex");
    v.insert("V:papersize", "a4");
    v.insert("V:colorlinks", true);
    v.insert("M:title", "Mein Titel");
    v.insert(extraArgumentsKey(), "--dpi=150 -M \"subtitle=Mit Leerzeichen\"");

    const QStringList args = PandocFormats::optionArguments("pdf", v);
    const QStringList expected = {"--toc", "--toc-depth=2", "-M", "title=Mein Titel",
                                  "--pdf-engine=xelatex", "-V", "papersize=a4",
                                  "-V", "colorlinks=true",
                                  "--dpi=150", "-M", "subtitle=Mit Leerzeichen"};
    QCOMPARE(args, expected);

    QVariantMap html;
    html.insert("math", "mathjax");
    html.insert("--standalone", false);
    QCOMPARE(PandocFormats::optionArguments("html", html), QStringList{"--mathjax"});
}

void TestPandocUi::optionsWidgetRoundTrip()
{
    FormatOptionsWidget widget("pdf");
    QSignalSpy changed(&widget, &FormatOptionsWidget::optionsChanged);

    QVariantMap v;
    v.insert("--toc", true);
    v.insert("--pdf-engine", "lualatex");
    v.insert("V:geometry", "margin=2cm");
    v.insert(extraArgumentsKey(), "--dpi=300");
    widget.setValues(v);
    QCOMPARE(changed.count(), 1);

    const QVariantMap out = widget.values();
    QCOMPARE(out.value("--toc").toBool(), true);
    QCOMPARE(out.value("--pdf-engine").toString(), QStringLiteral("lualatex"));
    QCOMPARE(out.value("V:geometry").toString(), QStringLiteral("margin=2cm"));
    QCOMPARE(out.value(extraArgumentsKey()).toString(), QStringLiteral("--dpi=300"));
    QCOMPARE(PandocFormats::optionArguments("pdf", out),
             PandocFormats::optionArguments("pdf", v));

    widget.resetToDefaults();
    QCOMPARE(PandocFormats::optionArguments("pdf", widget.values()), QStringList{});
}

void TestPandocUi::mainWindowBuildsJobs()
{
    MainWindow window;
    QVERIFY(window.openInputFile(m_sample));
    QCOMPARE(window.currentInputFormat(), QStringLiteral("markdown"));

    window.setSelectedOutputFormats({"html", "pdf", "docx"});
    QCOMPARE(window.selectedOutputFormats(), (QStringList{"html", "pdf", "docx"}));
    QVERIFY(window.optionsWidget("pdf"));

    const QString outDir = m_tmp.filePath("out");
    window.setOutputDirectory(outDir);
    QCOMPARE(window.outputDirectory(), outDir);

    const QList<ConversionJob> jobs = window.buildJobs();
    QCOMPARE(jobs.size(), 3);
    QCOMPARE(jobs[0].format, QStringLiteral("html"));
    QCOMPARE(jobs[0].outputPath, QDir(outDir).filePath("beispiel.html"));
    QVERIFY(jobs[0].arguments.contains("-t"));
    QCOMPARE(jobs[0].arguments.mid(0, 4), (QStringList{"-f", "markdown", "-t", "html"}));
    QCOMPARE(jobs[1].outputPath, QDir(outDir).filePath("beispiel.pdf"));
    QVERIFY(!jobs[1].arguments.contains("-t")); // PDF ergibt sich aus der Endung
    QCOMPARE(QDir::fromNativeSeparators(jobs[2].arguments.last()), m_sample);

    // Manuelle Formatwahl überschreibt die Erkennung
    window.setInputFormat("commonmark");
    QCOMPARE(window.buildJobs().first().arguments.mid(0, 2), (QStringList{"-f", "commonmark"}));

    // Ausgabe nach Markdown im selben Ordner darf die Eingabe nicht überschreiben
    window.setOutputDirectory({});
    window.setSelectedOutputFormats({"markdown"});
    QCOMPARE(window.selectedOutputFormats(), QStringList{"markdown"});
    QVERIFY(!window.optionsWidget("pdf"));
    QVERIFY(!window.optionsWidget("html"));
    QCOMPARE(QFileInfo(window.outputPathFor("markdown")).fileName(),
             QStringLiteral("beispiel.markdown.md"));

    // Optional: Bildschirmfoto für die Dokumentation
    const QString shot = qEnvironmentVariable("PANDOCUI_SCREENSHOT");
    if (!shot.isEmpty()) {
        window.setInputFormat("markdown");
        window.findChild<QCheckBox *>("autoDetectCheck")->setChecked(true);
        window.setSelectedOutputFormats({"html", "pdf", "docx", "epub"});
        window.setOutputDirectory(outDir);
        if (FormatOptionsWidget *w = window.optionsWidget("pdf")) {
            QVariantMap v;
            v.insert("--toc", true);
            v.insert("--pdf-engine", "xelatex");
            v.insert("V:papersize", "a4");
            w->setValues(v);
        }
        window.presetStore().save(window.currentPreset("Bericht A4"));
        window.loadPreset("Bericht A4");
        window.resize(1180, 860);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::qWait(200);
        // Eine Aufnahme je Wizard-Schritt
        for (int step = 0; step < MainWindow::StepCount; ++step) {
            QVERIFY(window.setCurrentStep(step));
            QTest::qWait(50);
            QVERIFY(window.grab().save(QString(shot).replace(".png", QStringLiteral("_step%1.png").arg(step + 1))));
        }
    }
}

void TestPandocUi::addPdfToExistingSelection()
{
    MainWindow window;
    window.show();
    QVERIFY(window.openInputFile(m_sample));
    window.setSelectedOutputFormats({"html", "odt"});
    QCoreApplication::processEvents();
    const QSize minBefore = window.minimumSizeHint();

    // Wie ein Klick des Benutzers: Häkchen direkt am Listeneintrag setzen
    auto *tree = window.findChild<QTreeWidget *>();
    QVERIFY(tree);
    const QList<QTreeWidgetItem *> items = tree->findItems("pdf", Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(items.size(), 1);
    tree->setCurrentItem(items.first());
    items.first()->setCheckState(0, Qt::Checked);
    QCoreApplication::processEvents();

    QCOMPARE(window.selectedOutputFormats(), (QStringList{"html", "odt", "pdf"}));
    QVERIFY(window.optionsWidget("pdf"));
    QCOMPARE(window.buildJobs().size(), 3);

    // Die Mindestgröße des Fensters darf nicht mit der Formatauswahl wachsen.
    // Sonst bricht z. B. WSLg/Wayland bei maximiertem Fenster mit einem
    // Protokollfehler ab ("buffer does not match the configured maximized state").
    window.setSelectedOutputFormats({"html", "odt", "pdf", "docx", "epub", "latex", "gfm",
                                     "markdown", "revealjs", "beamer", "pptx", "rst"});
    QCoreApplication::processEvents();
    const QSize minAfter = window.minimumSizeHint();
    qInfo() << "Mindestgröße vorher" << minBefore << "nachher" << minAfter;
    QVERIFY2(minAfter.height() <= minBefore.height() && minAfter.width() <= minBefore.width(),
             qPrintable(QStringLiteral("%1x%2 -> %3x%4").arg(minBefore.width()).arg(minBefore.height())
                            .arg(minAfter.width()).arg(minAfter.height())));
}

void TestPandocUi::wizardNavigation()
{
    MainWindow window;
    window.setOutputDirectory({}); // Ordner der Eingabedatei
    window.setSelectedOutputFormats({"html"});
    QCOMPARE(window.currentStep(), int(MainWindow::InputStep));

    // Ohne Eingabedatei sind die folgenden Schritte gesperrt
    QVERIFY(!window.stepProblem(MainWindow::InputStep).isEmpty());
    QVERIFY(!window.isStepReachable(MainWindow::FormatsStep));
    QVERIFY(!window.setCurrentStep(MainWindow::FormatsStep));
    QCOMPARE(window.currentStep(), int(MainWindow::InputStep));

    QVERIFY(window.openInputFile(m_sample));
    QVERIFY(window.stepProblem(MainWindow::InputStep).isEmpty());
    for (int step = MainWindow::FormatsStep; step < MainWindow::StepCount; ++step)
        QVERIFY2(window.isStepReachable(step), qPrintable(QString::number(step)));

    QVERIFY(window.setCurrentStep(MainWindow::OutputStep));
    QCOMPARE(window.currentStep(), int(MainWindow::OutputStep));

    // Fällt die Formatauswahl weg (z. B. per Preset), springt der Wizard zu Schritt 2 zurück
    window.setSelectedOutputFormats({});
    QCOMPARE(window.currentStep(), int(MainWindow::FormatsStep));
    QVERIFY(!window.isStepReachable(MainWindow::OptionsStep));

    window.setSelectedOutputFormats({"gfm"});
    QVERIFY(window.setCurrentStep(MainWindow::RunStep));
    QCOMPARE(window.currentStep(), int(MainWindow::RunStep));
    QVERIFY(!window.setCurrentStep(MainWindow::StepCount)); // ungültig
}

void TestPandocUi::presetStoreRoundTrip()
{
    PresetStore store(m_tmp.filePath("presets"));
    QVERIFY(store.names().isEmpty());

    Preset preset;
    preset.name = "Bericht: A4/PDF ?*";
    preset.formats = {"pdf", "html"};
    preset.options["pdf"] = QVariantMap{{"--toc", true}, {"--toc-depth", 2},
                                        {"V:papersize", "a4"}, {extraArgumentsKey(), "--dpi=300"}};
    preset.options["html"] = QVariantMap{{"--embed-resources", true}};

    QString error;
    QVERIFY2(store.save(preset, &error), qPrintable(error));
    QCOMPARE(store.names(), QStringList{preset.name});
    QVERIFY(store.contains(preset.name));

    const std::optional<Preset> loaded = store.load(preset.name, &error);
    QVERIFY2(loaded, qPrintable(error));
    QCOMPARE(loaded->name, preset.name);
    QCOMPARE(loaded->formats, preset.formats);
    // Die erzeugten pandoc-Argumente müssen identisch sein (JSON speichert Zahlen als double)
    for (const QString &format : std::as_const(preset.formats)) {
        QCOMPARE(PandocFormats::optionArguments(format, loaded->options.value(format)),
                 PandocFormats::optionArguments(format, preset.options.value(format)));
    }

    QVERIFY(store.remove(preset.name, &error));
    QVERIFY(store.names().isEmpty());
    QVERIFY(!store.save(Preset{}, &error)); // ohne Namen
}

void TestPandocUi::presetJsonErrors()
{
    QString error;
    QVERIFY(!PresetStore::fromJson("kein json", &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!PresetStore::fromJson("{\"name\": \"x\"}", &error));
    QVERIFY(!PresetStore::fromJson("{\"pandocui-preset\": 99}", &error));

    const auto preset = PresetStore::fromJson(
        "{\"pandocui-preset\": 1, \"formats\": [\"gfm\", \"gfm\", 3], \"options\": {}}", &error);
    QVERIFY(preset);
    QCOMPARE(preset->formats, QStringList{"gfm"});
    QVERIFY(preset->options.value("gfm").isEmpty());
}

void TestPandocUi::mainWindowPresets()
{
    MainWindow window;
    window.setSelectedOutputFormats({"html", "pdf"});
    const QVariantMap pdf{{"--toc", true}, {"--pdf-engine", "xelatex"}, {"V:geometry", "margin=2cm"}};
    window.optionsWidget("pdf")->setValues(pdf);

    // Presets enthalten nur vom Standard abweichende Werte
    const Preset current = window.currentPreset("PDF und HTML");
    QCOMPARE(current.options.value("pdf"), pdf);
    QVERIFY(current.options.value("html").isEmpty());

    QString error;
    QVERIFY2(window.presetStore().save(current, &error), qPrintable(error));

    // Auswahl und Optionen ändern, dann Preset laden
    window.optionsWidget("pdf")->resetToDefaults();
    window.setSelectedOutputFormats({"docx"});
    QVERIFY(window.loadPreset("PDF und HTML"));
    QCOMPARE(window.selectedOutputFormats(), (QStringList{"html", "pdf"}));
    QCOMPARE(window.currentPresetName(), QStringLiteral("PDF und HTML"));
    QVERIFY(!window.optionsWidget("docx"));
    QCOMPARE(PandocFormats::optionArguments("pdf", window.optionsWidget("pdf")->values()),
             PandocFormats::optionArguments("pdf", pdf));

    // Nicht vorhandene Formate werden übersprungen und gemeldet
    Preset foreign;
    foreign.name = "Fremd";
    foreign.formats = {"gibtsnicht", "gfm"};
    QCOMPARE(window.applyPreset(foreign), QStringList{"gibtsnicht"});
    QCOMPARE(window.selectedOutputFormats(), QStringList{"gfm"});
}

void TestPandocUi::installHints_data()
{
    QTest::addColumn<QString>("productType");
    QTest::addColumn<QStringList>("idLike");
    QTest::addColumn<QString>("commandPart");

    QTest::newRow("Windows") << "windows" << QStringList{} << "winget install";
    QTest::newRow("macOS") << "macos" << QStringList{} << "brew install pandoc";
    QTest::newRow("Debian") << "debian" << QStringList{} << "apt install pandoc";
    QTest::newRow("Ubuntu") << "ubuntu" << QStringList{"debian"} << "apt install pandoc";
    QTest::newRow("Linux Mint") << "linuxmint" << QStringList{"ubuntu", "debian"} << "apt install pandoc";
    QTest::newRow("Fedora") << "fedora" << QStringList{} << "dnf install pandoc";
    QTest::newRow("Rocky") << "rocky" << QStringList{"rhel", "centos", "fedora"} << "epel-release";
    QTest::newRow("AlmaLinux") << "almalinux" << QStringList{"rhel", "centos", "fedora"} << "epel-release";
    QTest::newRow("openSUSE") << "opensuse-tumbleweed" << QStringList{"opensuse", "suse"} << "zypper";
    QTest::newRow("Arch") << "arch" << QStringList{} << "pacman -S pandoc";
    QTest::newRow("EndeavourOS") << "endeavouros" << QStringList{"arch"} << "pacman -S pandoc";
    QTest::newRow("unbekannt") << "gentoo" << QStringList{} << "";
}

void TestPandocUi::installHints()
{
    QFETCH(QString, productType);
    QFETCH(QStringList, idLike);
    QFETCH(QString, commandPart);

    const Pandoc::InstallHint hint = Pandoc::installHint(productType, idLike);
    QVERIFY(!hint.systemName.isEmpty());
    QVERIFY(!hint.note.isEmpty());
    const QString commands = hint.commands.join(QLatin1Char('\n'));
    if (commandPart.isEmpty())
        QVERIFY(hint.commands.isEmpty());
    else
        QVERIFY2(commands.contains(commandPart), qPrintable(commands));
    // Fedora braucht kein EPEL
    if (productType == QLatin1String("fedora"))
        QVERIFY(!commands.contains("epel"));
}

void TestPandocUi::pandocMissingDialog()
{
    const Pandoc::InstallHint hint = Pandoc::installHint("rocky", {"rhel", "fedora"});
    PandocMissingDialog dialog(hint);
    QCOMPARE(dialog.commandText(), hint.commands.join(QLatin1Char('\n')));
    QCOMPARE(dialog.commandText().count(QLatin1Char('\n')), 1); // zwei Befehle

    QSignalSpy recheck(&dialog, &PandocMissingDialog::recheckRequested);
    const QList<QPushButton *> buttons = dialog.findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == QCoreApplication::translate("PandocMissingDialog", "Erneut prüfen"))
            button->click();
    }
    QCOMPARE(recheck.count(), 1);

    // Ohne bekannte Befehle gibt es kein Befehlsfeld
    PandocMissingDialog generic(Pandoc::installHint("gentoo"));
    QVERIFY(generic.commandText().isEmpty());

    const QString shot = qEnvironmentVariable("PANDOCUI_SCREENSHOT");
    if (!shot.isEmpty()) {
        PandocMissingDialog debian(Pandoc::installHint("debian"));
        debian.setDetails(QCoreApplication::translate("Pandoc", "Das Programm pandoc wurde nicht gefunden."));
        debian.show();
        QVERIFY(QTest::qWaitForWindowExposed(&debian));
        QTest::qWait(100);
        QVERIFY(debian.grab().save(QString(shot).replace(".png", "_pandoc_missing.png")));
    }
}

void TestPandocUi::languageResolution()
{
    QCOMPARE(Translation::availableLanguages(), (QStringList{"de", "en"}));
    QCOMPARE(Translation::resolve("de"), QStringLiteral("de"));
    QCOMPARE(Translation::resolve("EN_us"), QStringLiteral("en"));
    // Leer bzw. unbekannt: Systemsprache, ansonsten Englisch – jedenfalls eine verfügbare
    QVERIFY(Translation::availableLanguages().contains(Translation::resolve({})));
    QVERIFY(Translation::availableLanguages().contains(Translation::resolve("fr")));
    QCOMPARE(Translation::nativeName("en"), QStringLiteral("English"));
}

void TestPandocUi::englishTranslation()
{
    QCOMPARE(Translation::install(qApp, "en"), QStringLiteral("en"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "Konvertieren"), QStringLiteral("Convert"));
    QCOMPARE(PandocFormats::description("pdf"), QStringLiteral("PDF document"));
    QCOMPARE(PandocFormats::optionGroups("html").first().title, QStringLiteral("General"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "Fertig: %n Datei(en) erzeugt.", nullptr, 1),
             QStringLiteral("Done: 1 file generated."));
    QCOMPARE(QCoreApplication::translate("MainWindow", "Fertig: %n Datei(en) erzeugt.", nullptr, 3),
             QStringLiteral("Done: 3 files generated."));

    {
        MainWindow window;
        QCOMPARE(window.windowTitle(), QStringLiteral("PandocUi – convert documents with pandoc"));
        QVERIFY(window.openInputFile(m_sample));
        window.setSelectedOutputFormats({"html", "pdf", "docx"});
        // Abgeleiteter Dateiname bleibt unabhängig von der Sprache
        QCOMPARE(QFileInfo(window.outputPathFor("pdf")).fileName(), QStringLiteral("beispiel.pdf"));

        const QString shot = qEnvironmentVariable("PANDOCUI_SCREENSHOT");
        if (!shot.isEmpty()) {
            window.setOutputDirectory(m_tmp.filePath("out"));
            window.resize(1180, 860);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            QTest::qWait(200);
            QVERIFY(window.grab().save(QString(shot).replace(".png", "_en.png")));
        }
    }

    // Zurück zur Quellsprache
    QCOMPARE(Translation::install(qApp, "de"), QStringLiteral("de"));
    QCOMPARE(QCoreApplication::translate("MainWindow", "Konvertieren"), QStringLiteral("Konvertieren"));
    QCOMPARE(PandocFormats::description("pdf"), QStringLiteral("PDF-Dokument"));
}

void TestPandocUi::helpAnchors()
{
    QCOMPARE(HelpDialog::headingAnchor("1. Installation"), QStringLiteral("1-installation"));
    QCOMPARE(HelpDialog::headingAnchor("8. Schritt 5: Erzeugen"), QStringLiteral("8-schritt-5-erzeugen"));
    QCOMPARE(HelpDialog::headingAnchor("10. Sprache der Oberfläche"),
             QStringLiteral("10-sprache-der-oberfläche"));
    QCOMPARE(HelpDialog::manualUrl("de").toString(), QStringLiteral("qrc:/manual/BENUTZERHANDBUCH.md"));
    QCOMPARE(HelpDialog::manualUrl("en").toString(), QStringLiteral("qrc:/manual/USER_MANUAL.md"));
    QCOMPARE(HelpDialog::manualUrl("fr").toString(), QStringLiteral("qrc:/manual/USER_MANUAL.md"));
}

void TestPandocUi::helpDialog_data()
{
    QTest::addColumn<QString>("language");
    QTest::addColumn<QString>("title");
    QTest::newRow("de") << "de" << "PandocUi – Benutzerhandbuch";
    QTest::newRow("en") << "en" << "PandocUi – User Manual";
}

void TestPandocUi::helpDialog()
{
    QFETCH(QString, language);
    QFETCH(QString, title);

    HelpDialog dialog(nullptr, language);
    dialog.resize(900, 700);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QTextDocument *doc = dialog.browser()->document();
    QCOMPARE(doc->begin().text(), title);
    QVERIFY(doc->begin().blockFormat().headingLevel() == 1);

    // Jeder interne Link (Inhaltsverzeichnis) führt zu einer Überschrift,
    // jedes Bild ist eingebettet
    int internalLinks = 0;
    int images = 0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isAnchor() && format.anchorHref().startsWith(QLatin1Char('#'))) {
                ++internalLinks;
                QVERIFY2(dialog.scrollToAnchor(format.anchorHref().mid(1)),
                         qPrintable(format.anchorHref()));
            }
            if (format.isImageFormat()) {
                ++images;
                const QString name = format.toImageFormat().name();
                QVERIFY2(QFile::exists(QStringLiteral(":/manual/") + name), qPrintable(name));
                // nicht breiter als das Fenster
                QVERIFY(format.toImageFormat().width() > 0);
                QVERIFY2(format.toImageFormat().width() <= dialog.browser()->viewport()->width(),
                         qPrintable(QStringLiteral("%1: %2 > %3")
                                        .arg(name)
                                        .arg(format.toImageFormat().width())
                                        .arg(dialog.browser()->viewport()->width())));
            }
        }
    }
    QVERIFY(internalLinks >= 13);
    QCOMPARE(images, 7);

    const QString shot = qEnvironmentVariable("PANDOCUI_SCREENSHOT");
    if (!shot.isEmpty()) {
        QVERIFY(dialog.scrollToAnchor(language == QLatin1String("de") ? "6-schritt-3-optionen"
                                                                      : "6-step-3-options"));
        QTest::qWait(100);
        QVERIFY(dialog.grab().save(QString(shot).replace(".png", "_help_" + language + ".png")));
    }
    QVERIFY(!dialog.scrollToAnchor("gibt-es-nicht"));

    // Suche: "xattr" kommt genau einmal vor – die zweite Suche läuft am Ende um
    // und landet wieder auf demselben Treffer, ebenso rückwärts
    QVERIFY(dialog.findText("xattr"));
    const int first = dialog.browser()->textCursor().position();
    QVERIFY(dialog.findText("xattr"));
    QCOMPARE(dialog.browser()->textCursor().position(), first);
    QVERIFY(dialog.findText("xattr", true));
    QCOMPARE(dialog.browser()->textCursor().selectedText(), QStringLiteral("xattr"));
    QVERIFY(!dialog.findText("xyzzy-kommt-nicht-vor"));
}

void TestPandocUi::helpFromMainWindow()
{
    MainWindow window;
    HelpDialog *help = window.showManual();
    QVERIFY(help);
    QVERIFY(help->isVisible());
    QCOMPARE(window.showManual(), help); // wird wiederverwendet
    help->close();
}

void TestPandocUi::endToEndConversion()
{
    if (Pandoc::findExecutable().isEmpty())
        QSKIP("pandoc ist nicht installiert");

    MainWindow window;
    QVERIFY(!window.pandocExecutable().isEmpty());
    QVERIFY(window.openInputFile(m_sample));

    QStringList formats = {"html", "docx", "gfm", "odt"};
    if (!QStandardPaths::findExecutable("pdflatex").isEmpty())
        formats << "pdf";
    window.setSelectedOutputFormats(formats);
    if (FormatOptionsWidget *w = window.optionsWidget("html")) {
        QVariantMap v;
        v.insert("--toc", true);
        v.insert("--embed-resources", true);
        w->setValues(v);
    }

    const QString outDir = m_tmp.filePath("e2e");
    QDir().mkpath(outDir);
    window.setOutputDirectory(outDir);

    ConversionRunner runner;
    QSignalSpy finished(&runner, &ConversionRunner::finished);
    QSignalSpy jobFinished(&runner, &ConversionRunner::jobFinished);
    runner.start(window.pandocExecutable(), QFileInfo(m_sample).absolutePath(), window.buildJobs());
    QVERIFY(finished.wait(120000));

    for (const QList<QVariant> &call : std::as_const(jobFinished)) {
        if (!call.at(2).toBool())
            qWarning() << "Fehler:" << call.at(3).toString();
    }
    QCOMPARE(finished.first().at(0).toInt(), int(formats.size()));
    QCOMPARE(finished.first().at(1).toInt(), 0);

    for (const QString &format : std::as_const(formats)) {
        const QFileInfo fi(window.outputPathFor(format));
        QVERIFY2(fi.exists() && fi.size() > 0, qPrintable(fi.filePath()));
    }
    QFile html(window.outputPathFor("html"));
    QVERIFY(html.open(QIODevice::ReadOnly));
    const QByteArray content = html.readAll();
    QVERIFY(content.contains("<nav id=\"TOC\""));
    QVERIFY(content.contains("Beispieldokument"));
}

QTEST_MAIN(TestPandocUi)
#include "tst_pandocui.moc"
