#include "mainwindow.h"

#include "formatoptionswidget.h"
#include "pandocformats.h"

#include <QAction>
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
    updateState();
}

MainWindow::~MainWindow() = default;

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
        else
            choosePandocExecutable();
    });
    mainLayout->addWidget(m_pandocBanner);

    // --- 1. Eingabedatei --------------------------------------------------
    m_inputGroup = new QGroupBox(tr("1. Eingabedatei"));
    auto *inputGrid = new QGridLayout(m_inputGroup);

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
    m_autoDetectCheck->setChecked(true);
    connect(m_autoDetectCheck, &QCheckBox::toggled, this, &MainWindow::onAutoDetectToggled);

    m_detectLabel = new QLabel;
    m_detectLabel->setTextFormat(Qt::RichText);

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(m_inputFormatCombo);
    formatRow->addWidget(m_autoDetectCheck);
    formatRow->addSpacing(12);
    formatRow->addWidget(m_detectLabel, 1);

    inputGrid->addWidget(new QLabel(tr("Datei:")), 0, 0);
    inputGrid->addWidget(m_inputEdit, 0, 1);
    inputGrid->addWidget(browseInput, 0, 2);
    inputGrid->addWidget(new QLabel(tr("Eingabeformat:")), 1, 0);
    inputGrid->addLayout(formatRow, 1, 1, 1, 2);
    inputGrid->setColumnStretch(1, 1);
    mainLayout->addWidget(m_inputGroup);

    // --- 2. Ausgabeformate --------------------------------------------------
    auto *formatsGroup = new QGroupBox(tr("2. Ausgabeformate"));
    auto *formatsLayout = new QVBoxLayout(formatsGroup);
    m_formatFilter = new QLineEdit;
    m_formatFilter->setPlaceholderText(tr("Formate filtern …"));
    m_formatFilter->setClearButtonEnabled(true);
    connect(m_formatFilter, &QLineEdit::textChanged, this, &MainWindow::applyFormatFilter);

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
    formatsLayout->addWidget(m_formatFilter);
    formatsLayout->addWidget(m_outputTree, 1);
    formatsLayout->addWidget(m_selectionLabel);

    // --- 3. Optionen -----------------------------------------------------
    auto *optionsGroup = new QGroupBox(tr("3. Optionen je Ausgabeformat"));
    auto *optionsLayout = new QVBoxLayout(optionsGroup);
    m_optionsStack = new QStackedWidget;
    auto *hint = new QLabel(tr("Bitte links mindestens ein Ausgabeformat ankreuzen.\n"
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

    m_formatSplitter = new QSplitter(Qt::Horizontal);
    m_formatSplitter->addWidget(formatsGroup);
    m_formatSplitter->addWidget(optionsGroup);
    m_formatSplitter->setStretchFactor(0, 2);
    m_formatSplitter->setStretchFactor(1, 3);
    m_formatSplitter->setChildrenCollapsible(false);

    // --- 4. Ausgabeordner ------------------------------------------------
    m_outputGroup = new QGroupBox(tr("4. Ausgabeordner"));
    auto *outputGrid = new QGridLayout(m_outputGroup);
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
    m_outputPreview->setTextFormat(Qt::RichText);
    m_outputPreview->setWordWrap(true);
    m_outputPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_outputPreview->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Die Liste wächst mit der Formatauswahl. In einem Bereich fester Höhe darf sie
    // die Mindestgröße des Fensters nicht verändern (sonst Wayland-Protokollfehler
    // bei maximiertem Fenster).
    auto *previewScroll = new QScrollArea;
    previewScroll->setWidget(m_outputPreview);
    previewScroll->setWidgetResizable(true);
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewScroll->setFixedHeight(QFontMetrics(m_outputPreview->font()).lineSpacing() * 3 + 6);
    previewScroll->viewport()->setAutoFillBackground(false);
    m_outputPreview->setAutoFillBackground(false);

    outputGrid->addWidget(new QLabel(tr("Ordner:")), 0, 0);
    outputGrid->addWidget(m_outputDirEdit, 0, 1);
    outputGrid->addWidget(m_outputDirButton, 0, 2);
    outputGrid->addWidget(m_sameDirCheck, 0, 3);
    outputGrid->addWidget(new QLabel(tr("Dateien:")), 1, 0, Qt::AlignTop);
    outputGrid->addWidget(previewScroll, 1, 1, 1, 3);
    outputGrid->setColumnStretch(1, 1);

    // --- 5. Konvertierung ------------------------------------------------
    auto *runGroup = new QGroupBox(tr("5. Ausgabe erzeugen"));
    auto *runLayout = new QVBoxLayout(runGroup);
    auto *buttonRow = new QHBoxLayout;
    m_convertButton = new QPushButton(style()->standardIcon(QStyle::SP_MediaPlay),
                                      tr("Konvertieren"));
    m_convertButton->setDefault(true);
    QFont bold = m_convertButton->font();
    bold.setBold(true);
    m_convertButton->setFont(bold);
    m_convertButton->setMinimumWidth(160);
    connect(m_convertButton, &QPushButton::clicked, this, &MainWindow::startConversion);
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

    buttonRow->addWidget(m_convertButton);
    buttonRow->addWidget(m_cancelButton);
    buttonRow->addWidget(m_progress, 1);
    buttonRow->addWidget(m_openFolderCheck);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(5000);
    m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_log->setPlaceholderText(tr("Protokoll der Konvertierung"));
    m_log->setMinimumHeight(48);

    runLayout->addLayout(buttonRow);
    runLayout->addWidget(m_log, 1);

    auto *bottom = new QWidget;
    auto *bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addWidget(m_outputGroup);
    bottomLayout->addWidget(runGroup, 1);

    m_mainSplitter = new QSplitter(Qt::Vertical);
    m_mainSplitter->addWidget(m_formatSplitter);
    m_mainSplitter->addWidget(bottom);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 2);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setSizes({560, 280});
    mainLayout->addWidget(m_mainSplitter, 1);

    setCentralWidget(central);

    m_pandocStatus = new QLabel;
    statusBar()->addPermanentWidget(m_pandocStatus);

    resize(1180, 860);
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
    fileMenu->addSeparator();
    QAction *quitAction = fileMenu->addAction(tr("&Beenden"), this, &QWidget::close);
    quitAction->setShortcut(QKeySequence::Quit);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Hilfe"));
    helpMenu->addAction(tr("pandoc-&Handbuch (online)"), this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://pandoc.org/MANUAL.html")));
    });
    helpMenu->addSeparator();
    helpMenu->addAction(tr("Ü&ber PandocUi"), this, &MainWindow::showAbout);
    helpMenu->addAction(tr("Über &Qt"), qApp, &QApplication::aboutQt);

    m_busyDisabledActions = {openAction, convertAction, pandocAction};
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
    m_mainSplitter->restoreState(settings.value(QStringLiteral("mainSplitter")).toByteArray());
    m_formatSplitter->restoreState(settings.value(QStringLiteral("formatSplitter")).toByteArray());
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
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("mainSplitter"), m_mainSplitter->saveState());
    settings.setValue(QStringLiteral("formatSplitter"), m_formatSplitter->saveState());

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
            tr("<b>pandoc wurde nicht gefunden.</b> Bitte pandoc installieren "
               "(<a href=\"https://pandoc.org/installing.html\">pandoc.org</a>) oder das "
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
    if (!m_convertButton)
        return; // noch im Aufbau

    const bool hasInput = !m_inputPath.isEmpty() && QFileInfo(m_inputPath).isFile();
    const QString outDir = outputDirectory();

    // Vorschau der Ausgabedateien
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

    // Befehlsvorschau je Format
    const QString program = m_pandocPath.isEmpty() ? QStringLiteral("pandoc") : m_pandocPath;
    for (auto it = m_optionWidgets.cbegin(); it != m_optionWidgets.cend(); ++it)
        it.value()->setCommandPreview(ConversionRunner::formatCommandLine(program, argumentsFor(it.key())));

    m_selectionLabel->setText(m_selectedOrder.isEmpty()
                                  ? tr("Kein Format gewählt")
                                  : tr("%n Format(e) gewählt: %1", nullptr, m_selectedOrder.size())
                                        .arg(m_selectedOrder.join(QStringLiteral(", "))));
    m_selectionLabel->setToolTip(m_selectionLabel->text());

    // Bereitschaft prüfen
    QStringList missing;
    if (m_pandocPath.isEmpty())
        missing << tr("pandoc wurde nicht gefunden");
    if (!hasInput)
        missing << tr("Eingabedatei auswählen");
    else if (currentInputFormat().isEmpty())
        missing << tr("Eingabeformat auswählen");
    if (m_selectedOrder.isEmpty())
        missing << tr("mindestens ein Ausgabeformat ankreuzen");
    if (outDir.isEmpty() && hasInput)
        missing << tr("Ausgabeordner auswählen");

    const bool ready = missing.isEmpty() && !m_runner->isRunning();
    m_convertButton->setEnabled(ready);
    m_convertButton->setToolTip(missing.isEmpty()
                                    ? tr("Alle gewählten Formate erzeugen")
                                    : tr("Noch erforderlich:\n• %1").arg(missing.join(QStringLiteral("\n• "))));
}

void MainWindow::startConversion()
{
    updateState();
    if (m_runner->isRunning() || !m_convertButton->isEnabled())
        return;

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
}

void MainWindow::setBusy(bool busy)
{
    m_inputGroup->setEnabled(!busy);
    m_formatSplitter->setEnabled(!busy);
    m_outputGroup->setEnabled(!busy);
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
