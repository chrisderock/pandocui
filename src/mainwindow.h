#pragma once

#include "conversionrunner.h"
#include "pandocinfo.h"
#include "presetstore.h"

#include <QHash>
#include <QMainWindow>
#include <QVariantMap>

class FormatOptionsWidget;
class QAction;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTabWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Schritt 1: Eingabedatei setzen (erkennt das Format automatisch, sofern aktiviert)
    bool openInputFile(const QString &path);
    QString inputFile() const { return m_inputPath; }
    QString currentInputFormat() const;
    void setInputFormat(const QString &format); // schaltet auf manuelle Auswahl

    // Schritt 2: Ausgabeformate
    void setSelectedOutputFormats(const QStringList &formats);
    QStringList selectedOutputFormats() const { return m_selectedOrder; }

    // Schritt 3: Optionsmaske eines gewählten Formats
    FormatOptionsWidget *optionsWidget(const QString &format) const;

    // Schritt 4: Ausgabeordner (leer = Ordner der Eingabedatei)
    void setOutputDirectory(const QString &directory);
    QString outputDirectory() const;
    QString outputPathFor(const QString &format) const;

    // Schritt 5: Aufträge, die beim Konvertieren ausgeführt werden
    QList<ConversionJob> buildJobs() const;
    QString pandocExecutable() const { return m_pandocPath; }

    // Presets: Ausgabeformate samt Optionen
    Preset currentPreset(const QString &name = {}) const;
    // Übernimmt Formate und Optionen; liefert die nicht verfügbaren Formate zurück.
    QStringList applyPreset(const Preset &preset);
    bool loadPreset(const QString &name);
    QString currentPresetName() const { return m_currentPresetName; }
    const PresetStore &presetStore() const { return m_presets; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class LogLevel { Info, Success, Warning, Error, Command };

    void setupUi();
    void setupMenus();
    void loadSettings();
    void saveSettings();

    void loadPandoc();
    void populateInputFormats();
    void populateOutputFormats();

    void browseInputFile();
    void browseOutputDirectory();
    void choosePandocExecutable();
    void showAbout();

    void detectInputFormat();
    void onAutoDetectToggled(bool checked);
    void onSameDirToggled(bool checked);
    void onOutputItemChanged(QTreeWidgetItem *item, int column);
    void applyFormatFilter(const QString &text);
    QTreeWidgetItem *findFormatItem(const QString &format) const;
    void addOptionsTab(const QString &format);
    void removeOptionsTab(const QString &format);

    void refreshPresetList();
    void savePresetAs();
    void deleteCurrentPreset();
    void importPreset();
    void exportPreset();

    QStringList argumentsFor(const QString &format) const;
    void updateState();

    void startConversion();
    void onJobStarted(int index, int total, const ConversionJob &job);
    void onJobFinished(int index, const ConversionJob &job, bool success, const QString &message);
    void onConversionFinished(int succeeded, int failed, bool cancelled);
    void setBusy(bool busy);
    void appendLog(const QString &text, LogLevel level = LogLevel::Info);

    // Zustand
    Pandoc::Info m_pandocInfo;
    QString m_configuredPandocPath;
    QString m_pandocPath;
    QStringList m_inputFormats;
    QStringList m_outputFormats;
    QString m_inputPath;
    QString m_lastInputDir;
    QString m_customOutputDir;
    QStringList m_selectedOrder;
    QHash<QString, FormatOptionsWidget *> m_optionWidgets;
    QHash<QString, QVariantMap> m_savedOptions;
    ConversionRunner *m_runner;
    PresetStore m_presets;
    QString m_currentPresetName;

    // Oberfläche
    QLabel *m_pandocBanner = nullptr;
    QGroupBox *m_inputGroup = nullptr;
    QLineEdit *m_inputEdit = nullptr;
    QComboBox *m_inputFormatCombo = nullptr;
    QCheckBox *m_autoDetectCheck = nullptr;
    QLabel *m_detectLabel = nullptr;

    QSplitter *m_formatSplitter = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QToolButton *m_presetDeleteButton = nullptr;
    QLineEdit *m_formatFilter = nullptr;
    QTreeWidget *m_outputTree = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QStackedWidget *m_optionsStack = nullptr;
    QTabWidget *m_optionTabs = nullptr;

    QGroupBox *m_outputGroup = nullptr;
    QCheckBox *m_sameDirCheck = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    QPushButton *m_outputDirButton = nullptr;
    QLabel *m_outputPreview = nullptr;

    QPushButton *m_convertButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QCheckBox *m_openFolderCheck = nullptr;
    QProgressBar *m_progress = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QLabel *m_pandocStatus = nullptr;

    QSplitter *m_mainSplitter = nullptr;
    QList<QAction *> m_busyDisabledActions;
};
