#include "mainwindow.h"

#include "formatoptionswidget.h"
#include "pandocformats.h"
#include "pandocmissingdialog.h"
#include "stepnavigator.h"
#include "translation.h"

#include <QAction>
#include <QActionGroup>
#include <QProcess>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTime>
#include <QToolButton>
#include <QInputDialog>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int FormatRole = Qt::UserRole + 1;

QString native(const QString &path)
{
    return QDir::toNativeSeparators(path);
}

bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(QFileInfo(a).absoluteFilePath())
               .compare(QDir::cleanPath(QFileInfo(b).absoluteFilePath()), cs) == 0;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_runner(new ConversionRunner(this))
{
    setWindowTitle(tr("PandocUi – Dokumente mit pandoc umwandeln"));
    setAcceptDrops(true);

    setupUi();
    setupMenus();

    connect(m_runner, &ConversionRunner::jobStarted, this, &MainWindow::onJobStarted);
    connect(m_runner, &ConversionRunner::jobFinished, this, &MainWindow::onJobFinished);
    connect(m_runner, &ConversionRunner::finished, this, &MainWindow::onConversionFinished);
    connect(m_runner, &ConversionRunner::logMessage, this,
            [this](const QString &text) { appendLog(text, LogLevel::Command); });

    loadSettings();
    loadPandoc();
    refreshPresetList();
    m_visitedSteps.insert(InputStep);
    updateState();
}

MainWindow::~MainWindow()
{
    // Erst im Destruktor von QWidget wird das Fenster geschlossen. Dabei verliert z. B.
    // das Eingabefeld den Fokus und sendet editingFinished – zu diesem Zeitpunkt sind
    // die Mitglieder von MainWindow aber schon zerstört (unter Windows: Heap-Beschädigung).
    // Deshalb alle Verbindungen der eigenen Widgets zu diesem Fenster vorher trennen.
    const QList<QObject *> children = findChildren<QObject *>();
    for (QObject *child : children)
        QObject::disconnect(child, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------
// Aufbau der Oberfläche
// ---------------------------------------------------------------------------

void MainWindow::setupUi()
{
    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);

    // Hinweis, falls pandoc fehlt
    m_pandocBanner = new QLabel;
    m_pandocBanner->setWordWrap(true);
    m_pandocBanner->setTextFormat(Qt::RichText);
    m_pandocBanner->setStyleSheet(QStringLiteral(
        "QLabel { background: #fdecea; color: #8a1c13; border: 1px solid #f5c2bd;"
        " border-radius: 4px; padding: 6px; }"));
    m_pandocBanner->setVisible(false);
    connect(m_pandocBanner, &QLabel::linkActivated, this, [this](const QString &link) {
        if (link.startsWith(QLatin1String("http")))
            QDesktopServices::openUrl(QUrl(link));
        else if (link == QLatin1String("#help"))
            showPandocMissingDialog();
        else
            choosePandocExecutable();
    });
    mainLayout->addWidget(m_pandocBanner);

    // Texte der Schritte: Kurztitel (Schrittleiste), Seitentitel und Erklärung
    m_stepTitles = {tr("Eingabedatei"), tr("Ausgabeformate"), tr("Optionen"),
                    tr("Ausgabeordner"), tr("Erzeugen")};
    m_stepDescriptions = {
        tr("Die Datei auswählen, die umgewandelt werden soll. Das Format wird automatisch "
           "erkannt und lässt sich bei Bedarf von Hand festlegen."),
        tr("Ein oder mehrere Formate ankreuzen, in die das Dokument umgewandelt wird – "
           "oder ein gespeichertes Preset laden."),
        tr("Für jedes gewählte Format die Optionen festlegen. Unveränderte Werte "
           "überlässt PandocUi den Standardeinstellungen von pandoc."),
        tr("Festlegen, wohin die Dateien geschrieben werden. Die Dateinamen leiten sich "
           "vom Namen der Eingabedatei ab."),
        tr("Zusammenfassung prüfen und die Umwandlung starten."),
    };

    // --- Seite 1: Eingabedatei --------------------------------------------
    auto *inputPage = new QWidget;
    auto *inputGrid = new QGridLayout(inputPage);
    inputGrid->setContentsMargins(0, 0, 0, 0);

    m_inputEdit = new QLineEdit;
    m_inputEdit->setPlaceholderText(tr("Datei auswählen oder in das Fenster ziehen …"));
    auto *browseInput = new QPushButton(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                        tr("Durchsuchen …"));
    connect(browseInput, &QPushButton::clicked, this, &MainWindow::browseInputFile);
    connect(m_inputEdit, &QLineEdit::editingFinished, this, [this] {
        const QString text = QDir::fromNativeSeparators(m_inputEdit->text().trimmed());
        if (text.isEmpty()) {
            m_inputPath.clear();
            m_detectLabel->clear();
            updateState();
        } else if (!samePath(text, m_inputPath) || m_inputPath.isEmpty()) {
            openInputFile(text);
        }
    });

    m_inputFormatCombo = new QComboBox;
    m_inputFormatCombo->setMinimumWidth(260);
    m_inputFormatCombo->setEnabled(false);
    connect(m_inputFormatCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateState);

    m_autoDetectCheck = new QCheckBox(tr("Format automatisch erkennen"));
    m_autoDetectCheck->setObjectName(QStringLiteral("autoDetectCheck"));
    m_autoDetectCheck->setChecked(true);
    connect(m_autoDetectCheck, &QCheckBox::toggled, this, &MainWindow::onAutoDetectToggled);

    m_detectLabel = new QLabel;
    m_detectLabel->setTextFormat(Qt::RichText);
    m_detectLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(m_inputFormatCombo);
    formatRow->addWidget(m_autoDetectCheck);
    formatRow->addStretch(1);

    auto *dropHint = new QLabel(tr("Datei hierher ziehen"));
    dropHint->setAlignment(Qt::AlignCenter);
    dropHint->setMinimumHeight(90);
    dropHint->setMaximumHeight(200);
    dropHint->setEnabled(false);
    dropHint->setStyleSheet(QStringLiteral(
        "QLabel { border: 2px dashed palette(mid); border-radius: 8px; font-size: 13pt; }"));

    inputGrid->addWidget(new QLabel(tr("Datei:")), 0, 0);
    inputGrid->addWidget(m_inputEdit, 0, 1);
    inputGrid->addWidget(browseInput, 0, 2);
    inputGrid->addWidget(new QLabel(tr("Eingabeformat:")), 1, 0);
    inputGrid->addLayout(formatRow, 1, 1, 1, 2);
    inputGrid->addWidget(m_detectLabel, 2, 1, 1, 2);
    inputGrid->addItem(new QSpacerItem(0, 12), 3, 0);
    inputGrid->addWidget(dropHint, 4, 0, 1, 3);
    inputGrid->setRowStretch(4, 1);
    inputGrid->setRowStretch(5, 2);
    inputGrid->setColumnStretch(1, 1);

    // --- Seite 2: Ausgabeformate ------------------------------------------
    auto *formatsPage = new QWidget;
    auto *formatsLayout = new QVBoxLayout(formatsPage);
    formatsLayout->setContentsMargins(0, 0, 0, 0);

    // Presets: gespeicherte Kombinationen aus Formaten und Optionen
    auto *presetRow = new QHBoxLayout;
    m_presetCombo = new QComboBox;
    m_presetCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_presetCombo->setMinimumContentsLength(24);
    m_presetCombo->setToolTip(tr("Gespeichertes Preset auswählen: übernimmt Ausgabeformate "
                                 "und deren Optionen"));
    connect(m_presetCombo, &QComboBox::activated, this, [this](int index) {
        const QString name = m_presetCombo->itemData(index).toString();
        if (!name.isEmpty())
            loadPreset(name);
    });
    auto *presetSave = new QToolButton;
    presetSave->setIcon(style()->standardIcon(QStyle::SP_DialogSaveButton));
    presetSave->setToolTip(tr("Aktuelle Formate und Optionen als Preset speichern"));
    connect(presetSave, &QToolButton::clicked, this, &MainWindow::savePresetAs);
    m_presetDeleteButton = new QToolButton;
    m_presetDeleteButton->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_presetDeleteButton->setToolTip(tr("Gewähltes Preset löschen"));
    connect(m_presetDeleteButton, &QToolButton::clicked, this, &MainWindow::deleteCurrentPreset);
    m_formatFilter = new QLineEdit;
    m_formatFilter->setPlaceholderText(tr("Formate filtern …"));
    m_formatFilter->setClearButtonEnabled(true);
    connect(m_formatFilter, &QLineEdit::textChanged, this, &MainWindow::applyFormatFilter);
    presetRow->addWidget(new QLabel(tr("Preset:")));
    presetRow->addWidget(m_presetCombo);
    presetRow->addWidget(presetSave);
    presetRow->addWidget(m_presetDeleteButton);
    presetRow->addSpacing(16);
    presetRow->addWidget(m_formatFilter, 1);
    formatsLayout->addLayout(presetRow);

    m_outputTree = new QTreeWidget;
    m_outputTree->setColumnCount(2);
    m_outputTree->setHeaderLabels({tr("Format"), tr("Beschreibung")});
    m_outputTree->setRootIsDecorated(true);
    m_outputTree->setUniformRowHeights(true);
    m_outputTree->header()->setStretchLastSection(true);
    m_outputTree->setMinimumHeight(100);
    connect(m_outputTree, &QTreeWidget::itemChanged, this, &MainWindow::onOutputItemChanged);
    connect(m_outputTree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item)
            return;
        if (FormatOptionsWidget *w = m_optionWidgets.value(item->data(0, FormatRole).toString()))
            m_optionTabs->setCurrentWidget(w);
    });

    m_selectionLabel = new QLabel;
    // Lange Auswahllisten dürfen das Fenster nicht verbreitern
    m_selectionLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    formatsLayout->addWidget(m_outputTree, 1);
    formatsLayout->addWidget(m_selectionLabel);

    // --- Seite 3: Optionen ------------------------------------------------
    auto *optionsPage = new QWidget;
    auto *optionsLayout = new QVBoxLayout(optionsPage);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_optionsStack = new QStackedWidget;
    auto *hint = new QLabel(tr("Bitte zuerst in Schritt 2 mindestens ein Ausgabeformat ankreuzen.\n"
                               "Für jedes Format erscheint hier ein eigener Reiter mit Optionen."));
    hint->setAlignment(Qt::AlignCenter);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    m_optionTabs = new QTabWidget;
    m_optionTabs->setTabsClosable(true);
    m_optionTabs->setDocumentMode(true);
    connect(m_optionTabs, &QTabWidget::tabCloseRequested, this, [this](int index) {
        auto *w = qobject_cast<FormatOptionsWidget *>(m_optionTabs->widget(index));
        if (!w)
            return;
        if (QTreeWidgetItem *item = findFormatItem(w->format()))
            item->setCheckState(0, Qt::Unchecked); // entfernt den Reiter über onOutputItemChanged
        else
            removeOptionsTab(w->format());
    });
    m_optionsStack->addWidget(hint);
    m_optionsStack->addWidget(m_optionTabs);
    optionsLayout->addWidget(m_optionsStack);

    // Listen, die mit der Formatauswahl wachsen, kommen in Bereiche mit fester
    // Mindesthöhe. So ändert sich die Mindestgröße des Fensters nicht (sonst
    // Wayland-Protokollfehler bei maximiertem Fenster).
    auto makeScrollingLabel = [](QLabel *label) {
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        label->setAutoFillBackground(false);
        auto *scroll = new QScrollArea;
        scroll->setWidget(label);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMinimumHeight(QFontMetrics(label->font()).lineSpacing() * 3 + 6);
        scroll->viewport()->setAutoFillBackground(false);
        return scroll;
    };

    // --- Seite 4: Ausgabeordner -------------------------------------------
    auto *outputPage = new QWidget;
    auto *outputGrid = new QGridLayout(outputPage);
    outputGrid->setContentsMargins(0, 0, 0, 0);
    m_sameDirCheck = new QCheckBox(tr("Ordner der Eingabedatei verwenden"));
    connect(m_sameDirCheck, &QCheckBox::toggled, this, &MainWindow::onSameDirToggled);
    m_outputDirEdit = new QLineEdit;
    m_outputDirEdit->setPlaceholderText(tr("Zielordner wählen …"));
    connect(m_outputDirEdit, &QLineEdit::textChanged, this, [this] {
        if (!m_sameDirCheck->isChecked())
            m_customOutputDir = QDir::fromNativeSeparators(m_outputDirEdit->text().trimmed());
        updateState();
    });
    m_outputDirButton = new QPushButton(style()->standardIcon(QStyle::SP_DirOpenIcon),
                                        tr("Durchsuchen …"));
    connect(m_outputDirButton, &QPushButton::clicked, this, &MainWindow::browseOutputDirectory);
    m_outputPreview = new QLabel;

    outputGrid->addWidget(m_sameDirCheck, 0, 1, 1, 2);
    outputGrid->addWidget(new QLabel(tr("Ordner:")), 1, 0);
    outputGrid->addWidget(m_outputDirEdit, 1, 1);
    outputGrid->addWidget(m_outputDirButton, 1, 2);
    outputGrid->addItem(new QSpacerItem(0, 12), 2, 0);
    outputGrid->addWidget(new QLabel(tr("Dateien:")), 3, 0, Qt::AlignTop);
    outputGrid->addWidget(makeScrollingLabel(m_outputPreview), 3, 1, 1, 2);
    outputGrid->setRowStretch(3, 1);
    outputGrid->setColumnStretch(1, 1);

    // --- Seite 5: Erzeugen ------------------------------------------------
    auto *runPage = new QWidget;
    auto *runLayout = new QVBoxLayout(runPage);
    runLayout->setContentsMargins(0, 0, 0, 0);
    m_summaryLabel = new QLabel;
    runLayout->addWidget(makeScrollingLabel(m_summaryLabel), 2);

    auto *progressRow = new QHBoxLayout;
    m_cancelButton = new QPushButton(style()->standardIcon(QStyle::SP_BrowserStop),
                                     tr("Abbrechen"));
    m_cancelButton->setEnabled(false);
    connect(m_cancelButton, &QPushButton::clicked, m_runner, &ConversionRunner::cancel);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 1);
    m_progress->setValue(0);
    m_progress->setFormat(tr("%v von %m"));
    m_progress->setTextVisible(false);
    m_openFolderCheck = new QCheckBox(tr("Ausgabeordner danach öffnen"));
    progressRow->addWidget(m_progress, 1);
    progressRow->addWidget(m_cancelButton);
    progressRow->addWidget(m_openFolderCheck);
    runLayout->addLayout(progressRow);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText(tr("Protokoll der Konvertierung"));
    m_log->setMinimumHeight(48);
    runLayout->addWidget(m_log, 3);

    // --- Gerüst: Schrittleiste | Seite --------------------------------------
    m_navigator = new StepNavigator;
    m_navigator->setSteps(m_stepTitles);
    connect(m_navigator, &StepNavigator::stepClicked, this, &MainWindow::setCurrentStep);

    m_pages = new QStackedWidget;
    for (QWidget *page : {inputPage, formatsPage, optionsPage, outputPage, runPage})
        m_pages->addWidget(page);

    m_pageTitle = new QLabel;
    QFont titleFont = m_pageTitle->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.4);
    titleFont.setBold(true);
    m_pageTitle->setFont(titleFont);
    m_pageDescription = new QLabel;
    m_pageDescription->setWordWrap(true);
    m_pageDescription->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *pageArea = new QWidget;
    auto *pageLayout = new QVBoxLayout(pageArea);
    pageLayout->setContentsMargins(16, 4, 4, 0);
    pageLayout->addWidget(m_pageTitle);
    pageLayout->addWidget(m_pageDescription);
    auto *headerLine = new QFrame;
    headerLine->setFrameShape(QFrame::HLine);
    headerLine->setFrameShadow(QFrame::Sunken);
    pageLayout->addWidget(headerLine);
    pageLayout->addSpacing(6);
    pageLayout->addWidget(m_pages, 1);

    auto *body = new QHBoxLayout;
    body->addWidget(m_navigator);
    auto *navLine = new QFrame;
    navLine->setFrameShape(QFrame::VLine);
    navLine->setFrameShadow(QFrame::Sunken);
    body->addWidget(navLine);
    body->addWidget(pageArea, 1);
    mainLayout->addLayout(body, 1);

    // --- Fußzeile: Hinweis, Zurück, Weiter / Konvertieren -------------------
    auto *footerLine = new QFrame;
    footerLine->setFrameShape(QFrame::HLine);
    footerLine->setFrameShadow(QFrame::Sunken);
    mainLayout->addWidget(footerLine);

    auto *footer = new QHBoxLayout;
    m_footerHint = new QLabel;
    m_footerHint->setTextFormat(Qt::RichText);
    m_footerHint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_backButton = new QPushButton(style()->standardIcon(QStyle::SP_ArrowBack), tr("&Zurück"));
    connect(m_backButton, &QPushButton::clicked, this, [this] { setCurrentStep(currentStep() - 1); });
    m_nextButton = new QPushButton(tr("&Weiter"));
    m_nextButton->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_nextButton->setLayoutDirection(Qt::RightToLeft); // Pfeil rechts vom Text
    connect(m_nextButton, &QPushButton::clicked, this, [this] { setCurrentStep(currentStep() + 1); });
    m_convertButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
                                      tr("Konvertieren"));
    QFont bold = m_convertButton->font();
    bold.setBold(true);
    m_convertButton->setFont(bold);
    m_nextButton->setFont(bold);
    m_convertButton->setMinimumWidth(160);
    m_nextButton->setMinimumWidth(120);
    m_backButton->setMinimumWidth(120);
    connect(m_convertButton, &QPushButton::clicked, this, &MainWindow::startConversion);

    footer->addWidget(m_footerHint, 1);
    footer->addWidget(m_backButton);
    footer->addWidget(m_nextButton);
    footer->addWidget(m_convertButton);
    mainLayout->addLayout(footer);

    setCentralWidget(central);

    m_pandocStatus = new QLabel;
    statusBar()->addPermanentWidget(m_pandocStatus);

    resize(1100, 760);
}

void MainWindow::setupMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&Datei"));
    QAction *openAction = fileMenu->addAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                              tr("Eingabedatei &öffnen …"), this,
                                              &MainWindow::browseInputFile);
    openAction->setShortcut(QKeySequence::Open);
    QAction *convertAction = fileMenu->addAction(tr("&Konvertieren"), this,
                                                 &MainWindow::startConversion);
    convertAction->setShortcut(Qt::CTRL | Qt::Key_Return);
    fileMenu->addSeparator();
    QAction *pandocAction = fileMenu->addAction(tr("&pandoc-Programm festlegen …"), this,
                                                &MainWindow::choosePandocExecutable);

    QMenu *languageMenu = fileMenu->addMenu(tr("&Sprache"));
    auto *languageGroup = new QActionGroup(this);
    const QString configuredLanguage = Translation::languageSetting();
    auto addLanguage = [&](const QString &code, const QString &label) {
        QAction *action = languageMenu->addAction(label);
        action->setCheckable(true);
        action->setData(code);
        action->setChecked(code == configuredLanguage);
        languageGroup->addAction(action);
    };
    addLanguage(QString(), tr("Systemsprache"));
    languageMenu->addSeparator();
    const QStringList languages = Translation::availableLanguages();
    for (const QString &code : languages)
        addLanguage(code, Translation::nativeName(code));
    connect(languageGroup, &QActionGroup::triggered, this, &MainWindow::changeLanguage);
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Beenden"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu *presetMenu = menuBar()->addMenu(tr("&Presets"));
    QAction *savePresetAction = presetMenu->addAction(
        style()->standardIcon(QStyle::SP_DialogSaveButton), tr("Als Preset &speichern …"), this,
        &MainWindow::savePresetAs);
    savePresetAction->setShortcut(QKeySequence::Save);
    QAction *deletePresetAction = presetMenu->addAction(tr("Preset &löschen"), this,
                                                        &MainWindow::deleteCurrentPreset);
    presetMenu->addSeparator();
    QAction *importPresetAction = presetMenu->addAction(tr("Preset &importieren …"), this,
                                                        &MainWindow::importPreset);
    QAction *exportPresetAction = presetMenu->addAction(tr("Preset &exportieren …"), this,
                                                        &MainWindow::exportPreset);
    presetMenu->addSeparator();
    presetMenu->addAction(tr("Preset-&Ordner öffnen"), this, [this] {
        QDir().mkpath(m_presets.directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_presets.directory()));
    });

    QMenu *helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    helpMenu->addAction(tr("pandoc-&Handbuch (online)"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://pandoc.org/MANUAL.html")));
    });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Ü&ber PandocUi"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Über &Qt"), qApp, &QApplication::aboutQt);

    m_busyDisabledActions = {openAction,         convertAction,      pandocAction,
                             savePresetAction,   deletePresetAction, importPresetAction,
                             exportPresetAction};
}

// ---------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------

void MainWindow::loadSettings()
{
    QSettings settings;
    m_configuredPandocPath = settings.value(QStringLiteral("pandocPath")).toString();
    m_lastInputDir = settings.value(QStringLiteral("lastInputDir"), QDir::homePath()).toString();
    m_customOutputDir = settings.value(QStringLiteral("outputDir")).toString();
    m_selectedOrder = settings.value(QStringLiteral("selectedFormats"),
                                     QStringList{QStringLiteral("html"), QStringLiteral("docx")})
                          .toStringList();
    m_selectedOrder.removeDuplicates();
    m_currentPresetName = settings.value(QStringLiteral("currentPreset")).toString();

    settings.beginGroup(QStringLiteral("options"));
    const QStringList keys = settings.childKeys();
    for (const QString &format : keys)
        m_savedOptions.insert(format, settings.value(format).toMap());
    settings.endGroup();

    const bool sameDir = settings.value(QStringLiteral("useInputDir"), true).toBool();
    m_sameDirCheck->setChecked(sameDir);
    onSameDirToggled(sameDir);
    m_openFolderCheck->setChecked(settings.value(QStringLiteral("openFolderAfter"), false).toBool());

    restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
}

void MainWindow::saveSettings()
{
    for (auto it = m_optionWidgets.cbegin(); it != m_optionWidgets.cend(); ++it)
        m_savedOptions.insert(it.key(), it.value()->values());

    QSettings settings;
    settings.setValue(QStringLiteral("pandocPath"), m_configuredPandocPath);
    settings.setValue(QStringLiteral("lastInputDir"), m_lastInputDir);
    settings.setValue(QStringLiteral("outputDir"), m_customOutputDir);
    settings.setValue(QStringLiteral("useInputDir"), m_sameDirCheck->isChecked());
    settings.setValue(QStringLiteral("openFolderAfter"), m_openFolderCheck->isChecked());
    settings.setValue(QStringLiteral("selectedFormats"), m_selectedOrder);
    settings.setValue(QStringLiteral("currentPreset"), m_currentPresetName);
    settings.setValue(QStringLiteral("geometry"), saveGeometry());

    settings.remove(QStringLiteral("options"));
    settings.beginGroup(QStringLiteral("options"));
    for (auto it = m_savedOptions.cbegin(); it != m_savedOptions.cend(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
}

// ---------------------------------------------------------------------------
// pandoc und Formatlisten
// ---------------------------------------------------------------------------

void MainWindow::loadPandoc()
{
    const QString exe = Pandoc::findExecutable(m_configuredPandocPath);
    m_pandocInfo = Pandoc::query(exe);

    if (m_pandocInfo.ok) {
        m_pandocPath = exe;
        m_inputFormats = m_pandocInfo.inputFormats;
        m_outputFormats = m_pandocInfo.outputFormats;
        m_pandocBanner->setVisible(false);
        m_pandocStatus->setText(tr("%1 – %2").arg(m_pandocInfo.version, native(exe)));
        appendLog(tr("%1 gefunden: %2").arg(m_pandocInfo.version, native(exe)), LogLevel::Info);
    } else {
        m_pandocPath.clear();
        m_pandocBanner->setText(
            tr("<b>pandoc wurde nicht gefunden.</b> "
               "<a href=\"#help\">Installationshinweise anzeigen</a> oder das "
               "<a href=\"#choose\">pandoc-Programm manuell auswählen</a>.<br>%1")
                .arg(m_pandocInfo.error.toHtmlEscaped()));
        m_pandocBanner->setVisible(true);
        m_pandocStatus->setText(tr("pandoc nicht gefunden"));
        appendLog(tr("pandoc nicht gefunden: %1").arg(m_pandocInfo.error), LogLevel::Error);
    }

    if (m_inputFormats.isEmpty())
        m_inputFormats = PandocFormats::fallbackInputFormats();
    if (m_outputFormats.isEmpty())
        m_outputFormats = PandocFormats::fallbackOutputFormats();
    // PDF ist kein eigenes Writer-Format, wird aber über die Dateiendung erzeugt.
    if (!m_outputFormats.contains(QLatin1String("pdf")))
        m_outputFormats << QStringLiteral("pdf");

    populateInputFormats();
    populateOutputFormats();
    updateState();
}

void MainWindow::populateInputFormats()
{
    const QString previous = currentInputFormat();
    {
        const QSignalBlocker blocker(m_inputFormatCombo);
        m_inputFormatCombo->clear();
        QStringList formats = m_inputFormats;
        formats.sort(Qt::CaseInsensitive);
        for (const QString &format : std::as_const(formats)) {
            const QString desc = PandocFormats::description(format);
            m_inputFormatCombo->addItem(desc.isEmpty() ? format
                                                       : QStringLiteral("%1 – %2").arg(format, desc),
                                        format);
        }
        m_inputFormatCombo->setCurrentIndex(m_inputFormatCombo->findData(previous));
    }
    if (m_autoDetectCheck->isChecked())
        detectInputFormat();
}

void MainWindow::populateOutputFormats()
{
    {
        const QSignalBlocker blocker(m_outputTree);
        m_outputTree->clear();

        QFont bold = m_outputTree->font();
        bold.setBold(true);
        auto makeGroup = [&](const QString &title) {
            auto *group = new QTreeWidgetItem(m_outputTree, {title});
            group->setFlags(Qt::ItemIsEnabled);
            group->setFont(0, bold);
            group->setFirstColumnSpanned(true);
            return group;
        };
        QTreeWidgetItem *common = makeGroup(tr("Häufig verwendet"));
        QTreeWidgetItem *others = makeGroup(tr("Weitere Formate"));

        auto addFormat = [this](QTreeWidgetItem *parent, const QString &format) {
            auto *item = new QTreeWidgetItem(parent, {format, PandocFormats::description(format)});
            item->setData(0, FormatRole, format);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            item->setCheckState(0, m_selectedOrder.contains(format) ? Qt::Checked : Qt::Unchecked);
            item->setToolTip(0, tr("Ausgabe als %1 (Dateiendung %2)")
                                    .arg(format, PandocFormats::outputExtension(format)));
        };

        const QStringList commonFormats = PandocFormats::commonOutputFormats();
        for (const QString &format : commonFormats) {
            if (m_outputFormats.contains(format))
                addFormat(common, format);
        }
        QStringList rest = m_outputFormats;
        rest.sort(Qt::CaseInsensitive);
        for (const QString &format : std::as_const(rest)) {
            if (!commonFormats.contains(format))
                addFormat(others, format);
        }

        common->setExpanded(true);
        others->setExpanded(true);
        m_outputTree->resizeColumnToContents(0);
        m_outputTree->setColumnWidth(0, m_outputTree->columnWidth(0) + 24);
    }

    // Auswahl mit den verfügbaren Formaten abgleichen
    const QStringList selected = m_selectedOrder;
    for (const QString &format : selected) {
        if (!m_outputFormats.contains(format)) {
            m_selectedOrder.removeAll(format);
            removeOptionsTab(format);
        } else if (!m_optionWidgets.contains(format)) {
            addOptionsTab(format);
        }
    }
    applyFormatFilter(m_formatFilter->text());
}

// ---------------------------------------------------------------------------
// Schritt 1: Eingabe
// ---------------------------------------------------------------------------

bool MainWindow::openInputFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile()) {
        m_inputPath.clear();
        m_detectLabel->setText(tr("<span style=\"color:#c0392b\">Datei nicht gefunden.</span>"));
        updateState();
        return false;
    }

    m_inputPath = fi.absoluteFilePath();
    m_lastInputDir = fi.absolutePath();
    m_lastResult.clear();
    m_lastResultOk = false;
    {
        const QSignalBlocker blocker(m_inputEdit);
        m_inputEdit->setText(native(m_inputPath));
    }

    if (m_autoDetectCheck->isChecked())
        detectInputFormat();
    else
        m_detectLabel->setText(tr("Manuelle Auswahl"));

    if (m_sameDirCheck->isChecked())
        onSameDirToggled(true);
    appendLog(tr("Eingabedatei: %1").arg(native(m_inputPath)));
    updateState();
    return true;
}

QString MainWindow::currentInputFormat() const
{
    return m_inputFormatCombo->currentData().toString();
}

void MainWindow::setInputFormat(const QString &format)
{
    m_autoDetectCheck->setChecked(false);
    m_inputFormatCombo->setCurrentIndex(m_inputFormatCombo->findData(format));
}

void MainWindow::detectInputFormat()
{
    if (m_inputPath.isEmpty()) {
        m_detectLabel->clear();
        return;
    }

    QString reason;
    const QString format = PandocFormats::detectInputFormat(m_inputPath, m_inputFormats, &reason);
    const int index = m_inputFormatCombo->findData(format);
    if (format.isEmpty() || index < 0) {
        {
            const QSignalBlocker blocker(m_autoDetectCheck);
            m_autoDetectCheck->setChecked(false);
        }
        m_inputFormatCombo->setEnabled(true);
        m_inputFormatCombo->setCurrentIndex(-1);
        m_detectLabel->setText(tr("<span style=\"color:#c0392b\">Format nicht erkannt – "
                                  "bitte manuell auswählen.</span>"));
        return;
    }

    m_inputFormatCombo->setCurrentIndex(index);
    m_detectLabel->setText(tr("Erkannt: <b>%1</b> (%2)").arg(format, reason.toHtmlEscaped()));
}

void MainWindow::onAutoDetectToggled(bool checked)
{
    m_inputFormatCombo->setEnabled(!checked);
    if (checked)
        detectInputFormat();
    else if (!m_inputPath.isEmpty())
        m_detectLabel->setText(tr("Manuelle Auswahl"));
    updateState();
}

void MainWindow::browseInputFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("Eingabedatei auswählen"), m_lastInputDir,
        tr("Alle Dateien (*);;"
           "Markdown (*.md *.markdown *.txt *.rmd *.qmd);;"
           "Word (*.docx);;OpenDocument (*.odt);;HTML (*.html *.htm *.xhtml);;"
           "LaTeX (*.tex *.latex);;reStructuredText (*.rst);;Org-Mode (*.org);;"
           "E-Book (*.epub *.fb2);;Jupyter Notebook (*.ipynb);;Typst (*.typ);;"
           "DocBook/XML (*.xml *.dbk)"));
    if (!file.isEmpty())
        openInputFile(file);
}

// ---------------------------------------------------------------------------
// Schritt 2 und 3: Ausgabeformate und Optionen
// ---------------------------------------------------------------------------

void MainWindow::setSelectedOutputFormats(const QStringList &formats)
{
    // Kopie: das Abwählen verändert m_selectedOrder über onOutputItemChanged
    const QStringList previous = m_selectedOrder;
    for (const QString &format : previous) {
        if (!formats.contains(format)) {
            if (QTreeWidgetItem *item = findFormatItem(format))
                item->setCheckState(0, Qt::Unchecked);
        }
    }
    for (const QString &format : formats) {
        if (QTreeWidgetItem *item = findFormatItem(format))
            item->setCheckState(0, Qt::Checked);
    }

    // Reihenfolge (Aufträge und Reiter) an die übergebene Liste anpassen
    QStringList ordered;
    for (const QString &format : formats) {
        if (m_selectedOrder.contains(format) && !ordered.contains(format))
            ordered << format;
    }
    m_selectedOrder = ordered;
    for (int i = 0; i < m_selectedOrder.size(); ++i) {
        const int from = m_optionTabs->indexOf(m_optionWidgets.value(m_selectedOrder.at(i)));
        if (from >= 0 && from != i)
            m_optionTabs->tabBar()->moveTab(from, i);
    }
    updateState();
}

FormatOptionsWidget *MainWindow::optionsWidget(const QString &format) const
{
    return m_optionWidgets.value(format);
}

QTreeWidgetItem *MainWindow::findFormatItem(const QString &format) const
{
    for (int i = 0; i < m_outputTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_outputTree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j) {
            if (group->child(j)->data(0, FormatRole).toString() == format)
                return group->child(j);
        }
    }
    return nullptr;
}

void MainWindow::onOutputItemChanged(QTreeWidgetItem *item, int column)
{
    if (!item || !item->parent() || column != 0)
        return;

    const QString format = item->data(0, FormatRole).toString();
    const bool checked = item->checkState(0) == Qt::Checked;
    m_lastResult.clear();
    m_lastResultOk = false;
    if (checked && !m_selectedOrder.contains(format)) {
        m_selectedOrder.append(format);
        addOptionsTab(format);
    } else if (!checked && m_selectedOrder.contains(format)) {
        m_selectedOrder.removeAll(format);
        removeOptionsTab(format);
    }
    updateState();
}

void MainWindow::applyFormatFilter(const QString &text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_outputTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *group = m_outputTree->topLevelItem(i);
        int visible = 0;
        for (int j = 0; j < group->childCount(); ++j) {
            QTreeWidgetItem *item = group->child(j);
            const bool match = needle.isEmpty()
                               || item->text(0).contains(needle, Qt::CaseInsensitive)
                               || item->text(1).contains(needle, Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match)
                ++visible;
        }
        group->setHidden(visible == 0);
    }
}

void MainWindow::addOptionsTab(const QString &format)
{
    if (m_optionWidgets.contains(format))
        return;
    auto *widget = new FormatOptionsWidget(format);
    if (m_savedOptions.contains(format))
        widget->setValues(m_savedOptions.value(format));
    connect(widget, &FormatOptionsWidget::optionsChanged, this, &MainWindow::updateState);
    m_optionWidgets.insert(format, widget);

    const int index = m_optionTabs->addTab(widget, format);
    const QString desc = PandocFormats::description(format);
    m_optionTabs->setTabToolTip(index, desc.isEmpty() ? format : desc);
    m_optionTabs->setCurrentIndex(index);
    m_optionsStack->setCurrentIndex(1);
}

void MainWindow::removeOptionsTab(const QString &format)
{
    FormatOptionsWidget *widget = m_optionWidgets.take(format);
    if (!widget)
        return;
    m_savedOptions.insert(format, widget->values());
    const int index = m_optionTabs->indexOf(widget);
    if (index >= 0)
        m_optionTabs->removeTab(index);
    widget->deleteLater();
    if (m_optionTabs->count() == 0)
        m_optionsStack->setCurrentIndex(0);
}

// ---------------------------------------------------------------------------
// Presets
// ---------------------------------------------------------------------------

Preset MainWindow::currentPreset(const QString &name) const
{
    Preset preset;
    preset.name = name.isEmpty() ? m_currentPresetName : name;
    preset.formats = m_selectedOrder;
    for (const QString &format : m_selectedOrder) {
        const FormatOptionsWidget *widget = m_optionWidgets.value(format);
        const QVariantMap values = widget ? widget->values() : m_savedOptions.value(format);
        preset.options.insert(format, PandocFormats::nonDefaultValues(format, values));
    }
    return preset;
}

QStringList MainWindow::applyPreset(const Preset &preset)
{
    QStringList available;
    QStringList missing;
    for (const QString &format : preset.formats)
        (m_outputFormats.contains(format) ? available : missing) << format;

    // Optionen vorab hinterlegen, damit neu angelegte Reiter sie übernehmen …
    for (const QString &format : std::as_const(available))
        m_savedOptions.insert(format, preset.options.value(format));
    setSelectedOutputFormats(available);
    // … und in bereits vorhandenen Reitern setzen (fehlende Werte = Standard).
    for (const QString &format : std::as_const(available)) {
        if (FormatOptionsWidget *widget = m_optionWidgets.value(format))
            widget->setValues(preset.options.value(format));
    }

    m_currentPresetName = preset.name;
    refreshPresetList();
    updateState();
    return missing;
}

bool MainWindow::loadPreset(const QString &name)
{
    QString error;
    const std::optional<Preset> preset = m_presets.load(name, &error);
    if (!preset) {
        appendLog(tr("Preset „%1“ konnte nicht geladen werden: %2").arg(name, error), LogLevel::Error);
        QMessageBox::warning(this, tr("Preset laden"),
                             tr("Das Preset „%1“ konnte nicht geladen werden:\n%2").arg(name, error));
        refreshPresetList();
        return false;
    }

    const QStringList missing = applyPreset(*preset);
    appendLog(tr("Preset „%1“ geladen: %2").arg(name, preset->formats.join(QStringLiteral(", "))));
    if (!missing.isEmpty()) {
        appendLog(tr("Von dieser pandoc-Version nicht unterstützt und übersprungen: %1")
                      .arg(missing.join(QStringLiteral(", "))),
                  LogLevel::Warning);
    }
    statusBar()->showMessage(tr("Preset „%1“ geladen").arg(name), 5000);
    return true;
}

void MainWindow::refreshPresetList()
{
    const QStringList names = m_presets.names();
    if (!names.contains(m_currentPresetName))
        m_currentPresetName.clear();

    const QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    m_presetCombo->addItem(names.isEmpty() ? tr("(keine Presets gespeichert)")
                                           : tr("(Preset wählen)"),
                           QString());
    for (const QString &name : names)
        m_presetCombo->addItem(name, name);
    const int index = m_presetCombo->findData(m_currentPresetName);
    m_presetCombo->setCurrentIndex(index < 0 ? 0 : index);
    m_presetDeleteButton->setEnabled(!m_currentPresetName.isEmpty());
}

void MainWindow::savePresetAs()
{
    if (m_selectedOrder.isEmpty()) {
        QMessageBox::information(this, tr("Preset speichern"),
                                 tr("Bitte zuerst mindestens ein Ausgabeformat auswählen."));
        return;
    }

    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Preset speichern"),
        tr("Name des Presets für %1:").arg(m_selectedOrder.join(QStringLiteral(", "))),
        QLineEdit::Normal, m_currentPresetName, &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    if (m_presets.contains(name)
        && QMessageBox::question(this, tr("Preset überschreiben?"),
                                 tr("Ein Preset „%1“ existiert bereits. Überschreiben?").arg(name))
               != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!m_presets.save(currentPreset(name), &error)) {
        QMessageBox::critical(this, tr("Preset speichern"),
                              tr("Das Preset konnte nicht gespeichert werden:\n%1").arg(error));
        return;
    }
    m_currentPresetName = name;
    refreshPresetList();
    appendLog(tr("Preset „%1“ gespeichert.").arg(name), LogLevel::Success);
    statusBar()->showMessage(tr("Preset „%1“ gespeichert").arg(name), 5000);
}

void MainWindow::deleteCurrentPreset()
{
    const QString name = m_presetCombo->currentData().toString();
    if (name.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Preset löschen"),
                              tr("Das Preset „%1“ wirklich löschen?").arg(name))
        != QMessageBox::Yes) {
        return;
    }
    QString error;
    if (!m_presets.remove(name, &error)) {
        QMessageBox::critical(this, tr("Preset löschen"),
                              tr("Das Preset konnte nicht gelöscht werden:\n%1").arg(error));
        return;
    }
    m_currentPresetName.clear();
    refreshPresetList();
    appendLog(tr("Preset „%1“ gelöscht.").arg(name));
}

void MainWindow::importPreset()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("Preset importieren"),
                                                      m_lastInputDir,
                                                      tr("PandocUi-Presets (*.json);;Alle Dateien (*)"));
    if (file.isEmpty())
        return;

    QString error;
    std::optional<Preset> preset = PresetStore::importFromFile(file, &error);
    if (!preset) {
        QMessageBox::warning(this, tr("Preset importieren"),
                             tr("Die Datei konnte nicht importiert werden:\n%1").arg(error));
        return;
    }
    if (m_presets.contains(preset->name)
        && QMessageBox::question(this, tr("Preset überschreiben?"),
                                 tr("Ein Preset „%1“ existiert bereits. Überschreiben?")
                                     .arg(preset->name))
               != QMessageBox::Yes) {
        return;
    }
    if (!m_presets.save(*preset, &error)) {
        QMessageBox::critical(this, tr("Preset importieren"),
                              tr("Das Preset konnte nicht gespeichert werden:\n%1").arg(error));
        return;
    }
    loadPreset(preset->name);
}

void MainWindow::exportPreset()
{
    if (m_selectedOrder.isEmpty()) {
        QMessageBox::information(this, tr("Preset exportieren"),
                                 tr("Bitte zuerst mindestens ein Ausgabeformat auswählen."));
        return;
    }
    const QString name = m_currentPresetName.isEmpty() ? tr("preset") : m_currentPresetName;
    QString suggested = name;
    for (const QChar c : QStringLiteral("\\/:*?\"<>|"))
        suggested.replace(c, QLatin1Char('_'));
    const QString file = QFileDialog::getSaveFileName(
        this, tr("Preset exportieren"),
        QDir(m_lastInputDir).filePath(suggested + QStringLiteral(".json")),
        tr("PandocUi-Presets (*.json)"));
    if (file.isEmpty())
        return;

    QString error;
    if (!PresetStore::exportToFile(currentPreset(name), file, &error)) {
        QMessageBox::critical(this, tr("Preset exportieren"),
                              tr("Die Datei konnte nicht geschrieben werden:\n%1").arg(error));
        return;
    }
    appendLog(tr("Preset exportiert: %1").arg(native(file)), LogLevel::Success);
}

// ---------------------------------------------------------------------------
// Schritt 4: Ausgabeordner
// ---------------------------------------------------------------------------

void MainWindow::setOutputDirectory(const QString &directory)
{
    if (directory.isEmpty()) {
        m_sameDirCheck->setChecked(true);
        return;
    }
    m_sameDirCheck->setChecked(false);
    m_outputDirEdit->setText(native(directory));
}

QString MainWindow::outputDirectory() const
{
    if (m_sameDirCheck->isChecked())
        return m_inputPath.isEmpty() ? QString() : QFileInfo(m_inputPath).absolutePath();
    return QDir::fromNativeSeparators(m_outputDirEdit->text().trimmed());
}

void MainWindow::onSameDirToggled(bool checked)
{
    m_outputDirEdit->setEnabled(!checked);
    m_outputDirButton->setEnabled(!checked);
    {
        const QSignalBlocker blocker(m_outputDirEdit);
        if (checked)
            m_outputDirEdit->setText(m_inputPath.isEmpty()
                                         ? QString()
                                         : native(QFileInfo(m_inputPath).absolutePath()));
        else
            m_outputDirEdit->setText(native(m_customOutputDir));
    }
    updateState();
}

void MainWindow::browseOutputDirectory()
{
    QString start = outputDirectory();
    if (start.isEmpty())
        start = m_lastInputDir;
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Ausgabeordner auswählen"), start);
    if (!dir.isEmpty())
        m_outputDirEdit->setText(native(dir));
}

QString MainWindow::outputPathFor(const QString &format) const
{
    const QString base = m_inputPath.isEmpty() ? tr("ausgabe")
                                               : QFileInfo(m_inputPath).completeBaseName();
    QString name = PandocFormats::outputFileName(base, format, m_selectedOrder);
    QString dir = outputDirectory();
    if (dir.isEmpty())
        dir = QDir::currentPath();
    QString path = QDir(dir).absoluteFilePath(name);

    // Niemals die Eingabedatei überschreiben (z. B. markdown → markdown im selben Ordner)
    if (!m_inputPath.isEmpty() && samePath(path, m_inputPath)) {
        name = base + QLatin1Char('.') + format + PandocFormats::outputExtension(format);
        path = QDir(dir).absoluteFilePath(name);
    }
    return QDir::cleanPath(path);
}

// ---------------------------------------------------------------------------
// Schritt 5: Konvertierung
// ---------------------------------------------------------------------------

QStringList MainWindow::argumentsFor(const QString &format) const
{
    QStringList args;
    const QString inputFormat = currentInputFormat();
    if (!inputFormat.isEmpty())
        args << QStringLiteral("-f") << inputFormat;
    // Für PDF bestimmt pandoc den Weg über die Dateiendung (-t pdf ist nicht zulässig).
    if (format != QLatin1String("pdf"))
        args << QStringLiteral("-t") << format;

    const FormatOptionsWidget *widget = m_optionWidgets.value(format);
    const QVariantMap values = widget ? widget->values() : m_savedOptions.value(format);
    args << PandocFormats::optionArguments(format, values);

    args << QStringLiteral("-o") << native(outputPathFor(format));
    args << (m_inputPath.isEmpty() ? tr("<eingabedatei>") : native(m_inputPath));
    return args;
}

QList<ConversionJob> MainWindow::buildJobs() const
{
    QList<ConversionJob> jobs;
    for (const QString &format : m_selectedOrder)
        jobs.append({format, argumentsFor(format), outputPathFor(format)});
    return jobs;
}

void MainWindow::updateState()
{
    if (!m_convertButton || m_updating)
        return; // noch im Aufbau bzw. bereits in Arbeit
    m_updating = true;

    const bool hasInput = !m_inputPath.isEmpty() && QFileInfo(m_inputPath).isFile();

    // Vorschau der Ausgabedateien (Schritt 4)
    QStringList lines;
    for (const QString &format : std::as_const(m_selectedOrder)) {
        const QString path = outputPathFor(format);
        QString line = QStringLiteral("<b>%1</b> &nbsp;<span style=\"color:gray\">(%2)</span>")
                           .arg(QFileInfo(path).fileName().toHtmlEscaped(), format);
        if (hasInput && QFileInfo::exists(path))
            line += tr(" &nbsp;<i style=\"color:#d68910\">existiert – wird überschrieben</i>");
        lines << line;
    }
    m_outputPreview->setText(lines.isEmpty() ? tr("<i>Keine Ausgabeformate gewählt</i>")
                                             : lines.join(QStringLiteral("<br>")));

    // Befehlsvorschau je Format (Schritt 3)
    const QString program = m_pandocPath.isEmpty() ? QStringLiteral("pandoc") : m_pandocPath;
    for (auto it = m_optionWidgets.cbegin(); it != m_optionWidgets.cend(); ++it)
        it.value()->setCommandPreview(ConversionRunner::formatCommandLine(program, argumentsFor(it.key())));

    m_selectionLabel->setText(m_selectedOrder.isEmpty()
                                  ? tr("Kein Format gewählt")
                                  : tr("%n Format(e) gewählt: %1", nullptr, m_selectedOrder.size())
                                        .arg(m_selectedOrder.join(QStringLiteral(", "))));
    m_selectionLabel->setToolTip(m_selectionLabel->text());

    // Bereitschaft zum Konvertieren: alle Schritte vollständig
    QStringList missing;
    for (int step = 0; step < StepCount; ++step) {
        const QString problem = stepProblem(step);
        if (!problem.isEmpty())
            missing << problem;
    }
    const bool ready = missing.isEmpty() && !m_runner->isRunning();
    m_convertButton->setEnabled(ready);
    m_convertButton->setToolTip(missing.isEmpty()
                                    ? tr("Alle gewählten Formate erzeugen")
                                    : tr("Noch erforderlich:\n• %1").arg(missing.join(QStringLiteral("\n• "))));

    updateWizard();
    m_updating = false;
}

// ---------------------------------------------------------------------------
// Wizard
// ---------------------------------------------------------------------------

int MainWindow::currentStep() const
{
    return m_pages ? m_pages->currentIndex() : int(InputStep);
}

QString MainWindow::stepProblem(int step) const
{
    switch (step) {
    case InputStep:
        if (m_inputPath.isEmpty() || !QFileInfo(m_inputPath).isFile())
            return tr("Bitte eine Eingabedatei auswählen.");
        if (currentInputFormat().isEmpty())
            return tr("Bitte das Eingabeformat auswählen.");
        return {};
    case FormatsStep:
        return m_selectedOrder.isEmpty() ? tr("Bitte mindestens ein Ausgabeformat ankreuzen.")
                                         : QString();
    case OptionsStep:
        return {};
    case OutputStep:
        return outputDirectory().isEmpty() ? tr("Bitte einen Ausgabeordner wählen.") : QString();
    case RunStep:
        return m_pandocPath.isEmpty() ? tr("pandoc wurde nicht gefunden.") : QString();
    default:
        return {};
    }
}

bool MainWindow::isStepReachable(int step) const
{
    if (step < 0 || step >= StepCount)
        return false;
    for (int previous = 0; previous < step; ++previous) {
        if (!stepProblem(previous).isEmpty())
            return false;
    }
    return true;
}

bool MainWindow::setCurrentStep(int step)
{
    if (!isStepReachable(step) || (m_runner->isRunning() && step != RunStep))
        return false;
    m_pages->setCurrentIndex(step);
    m_visitedSteps.insert(step);
    if (!m_updating)
        updateState();
    return true;
}

void MainWindow::updateWizard()
{
    // Wurde der aktuelle Schritt unerreichbar (z. B. Preset ohne passende Formate
    // geladen), zum ersten unvollständigen Schritt zurückkehren.
    if (!isStepReachable(currentStep())) {
        int step = 0;
        while (step < currentStep() && stepProblem(step).isEmpty())
            ++step;
        m_pages->setCurrentIndex(step);
        m_visitedSteps.insert(step);
    }
    const int current = currentStep();
    const bool running = m_runner->isRunning();

    // Kurzfassungen in der Schrittleiste
    QString input = tr("Noch keine Datei gewählt");
    if (!m_inputPath.isEmpty()) {
        input = QFileInfo(m_inputPath).fileName();
        if (!currentInputFormat().isEmpty())
            input += QStringLiteral(" · ") + currentInputFormat();
    }
    int changedOptions = 0;
    for (const QString &format : std::as_const(m_selectedOrder)) {
        if (const FormatOptionsWidget *w = m_optionWidgets.value(format))
            changedOptions += int(PandocFormats::nonDefaultValues(format, w->values()).size());
    }
    const QString outDir = outputDirectory();
    QString run;
    if (running)
        run = tr("Konvertierung läuft …");
    else if (!m_lastResult.isEmpty())
        run = m_lastResult;
    else
        run = isStepReachable(RunStep) && stepProblem(RunStep).isEmpty() ? tr("Bereit")
                                                                          : tr("Noch nicht bereit");

    const QStringList summaries = {
        input,
        m_selectedOrder.isEmpty() ? tr("Kein Format gewählt") : m_selectedOrder.join(QStringLiteral(", ")),
        m_selectedOrder.isEmpty() ? QStringLiteral("–")
        : changedOptions == 0     ? tr("Standardeinstellungen")
                                  : tr("%n Option(en) geändert", nullptr, changedOptions),
        outDir.isEmpty() ? tr("Kein Ordner gewählt") : native(outDir),
        run,
    };

    m_navigator->setCurrent(current);
    for (int step = 0; step < StepCount; ++step) {
        const bool done = step == RunStep ? m_lastResultOk
                                          : m_visitedSteps.contains(step) && stepProblem(step).isEmpty();
        m_navigator->setStepState(step, isStepReachable(step) && (!running || step == RunStep), done);
        m_navigator->setSummary(step, summaries.value(step));
    }

    // Kopf der Seite
    m_pageTitle->setText(tr("Schritt %1 von %2: %3")
                             .arg(current + 1)
                             .arg(int(StepCount))
                             .arg(m_stepTitles.value(current)));
    m_pageDescription->setText(m_stepDescriptions.value(current));
    m_summaryLabel->setText(summaryHtml());

    // Fußzeile
    const QString problem = stepProblem(current);
    const bool lastStep = current == RunStep;
    m_backButton->setEnabled(current > 0 && !running);
    m_nextButton->setVisible(!lastStep);
    m_nextButton->setEnabled(problem.isEmpty() && !running);
    m_nextButton->setToolTip(problem);
    m_convertButton->setVisible(lastStep);
    if (!problem.isEmpty())
        m_footerHint->setText(QStringLiteral("<span style=\"color:#d68910\">⚠ %1</span>")
                                  .arg(problem.toHtmlEscaped()));
    else if (lastStep && !running && m_convertButton->isEnabled())
        m_footerHint->setText(tr("Alles bereit – „Konvertieren“ startet die Umwandlung."));
    else
        m_footerHint->clear();
}

QString MainWindow::summaryHtml() const
{
    const bool hasInput = !m_inputPath.isEmpty();
    QString html = QStringLiteral("<table cellspacing=\"6\">");
    auto row = [&html](const QString &label, const QString &value) {
        html += QStringLiteral("<tr><td valign=\"top\"><b>%1</b></td><td>%2</td></tr>").arg(label, value);
    };

    row(tr("Eingabedatei:"),
        hasInput ? QStringLiteral("%1 &nbsp;<span style=\"color:gray\">(%2)</span>")
                       .arg(native(m_inputPath).toHtmlEscaped(), currentInputFormat().toHtmlEscaped())
                 : QStringLiteral("–"));
    row(tr("Ausgabeordner:"),
        outputDirectory().isEmpty() ? QStringLiteral("–") : native(outputDirectory()).toHtmlEscaped());

    QStringList outputs;
    for (const QString &format : m_selectedOrder) {
        const FormatOptionsWidget *w = m_optionWidgets.value(format);
        const int changed = w ? int(PandocFormats::nonDefaultValues(format, w->values()).size()) : 0;
        const QString path = outputPathFor(format);
        QString line = QStringLiteral("<b>%1</b> &nbsp;<span style=\"color:gray\">(%2 · %3)</span>")
                           .arg(QFileInfo(path).fileName().toHtmlEscaped(), format,
                                changed == 0 ? tr("Standardeinstellungen")
                                             : tr("%n Option(en) geändert", nullptr, changed));
        if (hasInput && QFileInfo::exists(path))
            line += tr(" &nbsp;<i style=\"color:#d68910\">existiert – wird überschrieben</i>");
        outputs << line;
    }
    row(tr("Ausgabe:"), outputs.isEmpty() ? QStringLiteral("–") : outputs.join(QStringLiteral("<br>")));
    html += QStringLiteral("</table>");
    return html;
}

void MainWindow::startConversion()
{
    updateState();
    if (m_runner->isRunning() || !m_convertButton->isEnabled())
        return;
    setCurrentStep(RunStep); // z. B. bei Start über Strg+Eingabe aus einem früheren Schritt

    const QString outDir = outputDirectory();
    QDir dir(outDir);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        QMessageBox::critical(this, tr("Ausgabeordner"),
                              tr("Der Ausgabeordner „%1“ konnte nicht angelegt werden.").arg(native(outDir)));
        return;
    }

    const QList<ConversionJob> jobs = buildJobs();
    QStringList existing;
    for (const ConversionJob &job : jobs) {
        if (QFileInfo::exists(job.outputPath))
            existing << QFileInfo(job.outputPath).fileName();
    }
    if (!existing.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Dateien überschreiben?"),
            tr("Im Ausgabeordner existieren bereits:\n\n%1\n\nSollen diese Dateien überschrieben werden?")
                .arg(existing.join(QLatin1Char('\n'))));
        if (answer != QMessageBox::Yes)
            return;
    }

    saveSettings();
    m_lastResult.clear();
    m_lastResultOk = false;
    setBusy(true);
    m_progress->setRange(0, int(jobs.size()));
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    appendLog(tr("— Konvertierung von %1 (%2) in %n Format(e) —", nullptr, int(jobs.size()))
                  .arg(QFileInfo(m_inputPath).fileName(), currentInputFormat()));
    m_runner->start(m_pandocPath, QFileInfo(m_inputPath).absolutePath(), jobs);
}

void MainWindow::onJobStarted(int index, int total, const ConversionJob &job)
{
    statusBar()->showMessage(tr("Erzeuge %1 (%2 von %3) …").arg(job.format).arg(index + 1).arg(total));
}

void MainWindow::onJobFinished(int index, const ConversionJob &job, bool success, const QString &message)
{
    m_progress->setValue(index + 1);
    if (success) {
        appendLog(tr("✔ %1 → %2").arg(job.format, native(job.outputPath)), LogLevel::Success);
        if (!message.isEmpty())
            appendLog(message, LogLevel::Warning);
    } else {
        appendLog(tr("✘ %1 fehlgeschlagen").arg(job.format), LogLevel::Error);
        if (!message.isEmpty())
            appendLog(message, LogLevel::Error);
    }
}

void MainWindow::onConversionFinished(int succeeded, int failed, bool cancelled)
{
    m_lastResultOk = !cancelled && failed == 0;
    m_lastResult = cancelled ? tr("Abgebrochen")
                             : tr("%1 von %2 Dateien erzeugt").arg(succeeded).arg(succeeded + failed);
    setBusy(false);
    QString summary;
    if (cancelled)
        summary = tr("Abgebrochen: %1 erfolgreich, %2 nicht erzeugt.").arg(succeeded).arg(failed);
    else if (failed == 0)
        summary = tr("Fertig: %n Datei(en) erzeugt.", nullptr, succeeded);
    else
        summary = tr("Fertig mit Fehlern: %1 erfolgreich, %2 fehlgeschlagen.").arg(succeeded).arg(failed);
    appendLog(summary, failed == 0 && !cancelled ? LogLevel::Success : LogLevel::Warning);
    statusBar()->showMessage(summary, 10000);

    if (!cancelled && succeeded > 0 && m_openFolderCheck->isChecked())
        QDesktopServices::openUrl(QUrl::fromLocalFile(outputDirectory()));

    emit conversionFinished(succeeded, failed, cancelled);
}

void MainWindow::setBusy(bool busy)
{
    // Während der Umwandlung bleiben nur Schritt 5 und "Abbrechen" bedienbar
    for (int step = 0; step < RunStep; ++step)
        m_pages->widget(step)->setEnabled(!busy);
    m_openFolderCheck->setEnabled(!busy);
    m_cancelButton->setEnabled(busy);
    for (QAction *action : std::as_const(m_busyDisabledActions))
        action->setEnabled(!busy);
    if (busy)
        m_convertButton->setEnabled(false);
    else
        updateState();
}

void MainWindow::appendLog(const QString &text, LogLevel level)
{
    QString color;
    switch (level) {
    case LogLevel::Info:    color = QString(); break;
    case LogLevel::Success: color = QStringLiteral("#2e8b57"); break;
    case LogLevel::Warning: color = QStringLiteral("#d68910"); break;
    case LogLevel::Error:   color = QStringLiteral("#e74c3c"); break;
    case LogLevel::Command: color = QStringLiteral("#808080"); break;
    }
    QString html = text.toHtmlEscaped();
    html.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
    if (level == LogLevel::Command)
        html = QStringLiteral("$ ") + html;
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    const QString body = color.isEmpty()
        ? html
        : QStringLiteral("<span style=\"color:%1\">%2</span>").arg(color, html);
    m_log->appendHtml(QStringLiteral("<span style=\"color:#999\">%1</span> %2").arg(time, body));
}

// ---------------------------------------------------------------------------
// Sonstiges
// ---------------------------------------------------------------------------

void MainWindow::checkPandocAtStartup()
{
    if (!m_pandocInfo.ok)
        showPandocMissingDialog();
}

void MainWindow::showPandocMissingDialog()
{
    PandocMissingDialog dialog(Pandoc::installHintForThisSystem(), this);
    dialog.setDetails(m_pandocInfo.error);

    auto finishIfFound = [this, &dialog] {
        if (m_pandocInfo.ok) {
            appendLog(tr("pandoc ist jetzt verfügbar."), LogLevel::Success);
            dialog.accept();
        } else {
            dialog.setDetails(m_pandocInfo.error);
            dialog.setStatus(tr("pandoc wurde weiterhin nicht gefunden."));
        }
    };
    connect(&dialog, &PandocMissingDialog::recheckRequested, this, [this, finishIfFound] {
        loadPandoc();
        finishIfFound();
    });
    connect(&dialog, &PandocMissingDialog::chooseExecutableRequested, this, [this, finishIfFound] {
        choosePandocExecutable();
        finishIfFound();
    });
    dialog.exec();
}

void MainWindow::choosePandocExecutable()
{
#ifdef Q_OS_WIN
    const QString filter = tr("pandoc (pandoc.exe);;Programme (*.exe)");
#else
    const QString filter = tr("Alle Dateien (*)");
#endif
    const QString start = m_pandocPath.isEmpty() ? QDir::homePath()
                                                 : QFileInfo(m_pandocPath).absolutePath();
    const QString file = QFileDialog::getOpenFileName(this, tr("pandoc-Programm auswählen"),
                                                      start, filter);
    if (file.isEmpty())
        return;
    m_configuredPandocPath = file;
    loadPandoc();
    if (!m_pandocInfo.ok)
        QMessageBox::warning(this, tr("pandoc"),
                             tr("Die gewählte Datei konnte nicht als pandoc ausgeführt werden:\n%1")
                                 .arg(m_pandocInfo.error));
}

void MainWindow::changeLanguage(QAction *action)
{
    const QString code = action->data().toString();
    if (code == Translation::languageSetting())
        return;
    Translation::setLanguageSetting(code);

    // Gleiche wirksame Sprache (z. B. "Systemsprache" = Deutsch): kein Neustart nötig
    if (Translation::resolve(code) == QLocale().name().left(2))
        return;

    const auto answer = QMessageBox::question(
        this, tr("Sprache ändern"),
        tr("Die neue Sprache wird beim nächsten Start von PandocUi verwendet.\n"
           "Jetzt neu starten?"));
    if (answer != QMessageBox::Yes)
        return;

    QStringList arguments;
    if (!m_inputPath.isEmpty())
        arguments << m_inputPath;
    if (close())
        QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, tr("Über PandocUi"),
        tr("<h3>PandocUi %1</h3>"
           "<p>Grafische Oberfläche für <a href=\"https://pandoc.org\">pandoc</a>.</p>"
           "<p>Verwendetes pandoc: %2</p>"
           "<p>Erstellt mit Qt %3.</p>")
            .arg(QStringLiteral(PANDOCUI_VERSION),
                 m_pandocInfo.ok ? m_pandocInfo.version.toHtmlEscaped() : tr("nicht gefunden"),
                 QString::fromLatin1(qVersion())));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_runner->isRunning()) {
        const auto answer = QMessageBox::question(
            this, tr("Konvertierung läuft"),
            tr("Es läuft noch eine Konvertierung. Abbrechen und beenden?"));
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_runner->cancel();
    }
    saveSettings();
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!m_runner->isRunning() && !urls.isEmpty() && urls.first().isLocalFile()
        && QFileInfo(urls.first().toLocalFile()).isFile()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile()) {
        openInputFile(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}
