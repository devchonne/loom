#include "ui/SettingsDialog.h"

#include "theme/Fonts.h"
#include "theme/Theme.h"
#include "ui/ThemedDialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const Settings& settings, QWidget* parent)
    : QDialog(parent)
    , result_(settings) {
    setWindowTitle(QStringLiteral("settings"));
    setModal(true);
    // Taller now that the vault section is here.
    resize(460, 520);

    auto* form = new QFormLayout();
    auto* bodyFont = new QComboBox(this);
    bodyFont->setEditable(true);
    bodyFont->addItems({
        QStringLiteral("Departure Mono"),
        QStringLiteral("iA Writer Mono S"),
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("Noto Sans Mono"),
    });
    bodyFont->setCurrentText(settings.bodyFont);

    auto* chromeFont = new QComboBox(this);
    chromeFont->setEditable(true);
    chromeFont->addItems({
        QStringLiteral("Departure Mono"),
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("iA Writer Mono S"),
    });
    chromeFont->setCurrentText(settings.chromeFont);

    auto* size = new QDoubleSpinBox(this);
    size->setRange(8.0, 28.0);
    size->setSingleStep(0.5);
    size->setValue(settings.bodyPointSize);

    auto* lineHeight = new QDoubleSpinBox(this);
    lineHeight->setRange(1.0, 2.4);
    lineHeight->setSingleStep(0.05);
    lineHeight->setValue(settings.lineHeight);

    auto* theme = new QComboBox(this);
    QString lastGroup;
    for (const ThemeSpec& spec : Palettes::catalog()) {
        if (spec.group != lastGroup && !lastGroup.isEmpty()) {
            theme->insertSeparator(theme->count());
        }
        lastGroup = spec.group;
        theme->addItem(spec.name, spec.id);
    }
    const QString currentTheme = Palettes::normalize(settings.themeSource);
    const int themeIndex = theme->findData(currentTheme);
    theme->setCurrentIndex(themeIndex >= 0 ? themeIndex : 0);

    auto* scan = new QSlider(Qt::Horizontal, this);
    scan->setRange(0, 20);
    scan->setValue(int(settings.scanlineIntensity * 100));

    auto* blockCaret = new QCheckBox(QStringLiteral("block caret"), this);
    blockCaret->setChecked(settings.blockCaret);
    auto* crt = new QCheckBox(QStringLiteral("crt power-on wipe"), this);
    crt->setChecked(settings.crtWipe);
    auto* autosave = new QCheckBox(QStringLiteral("autosave named files"), this);
    autosave->setChecked(settings.autosaveNamedFiles);
    auto* zen = new QCheckBox(QStringLiteral("zen by default"), this);
    zen->setChecked(settings.zenByDefault);

    auto* notes = new QLineEdit(settings.notesDirectory, this);
    auto* browse = new QPushButton(QStringLiteral("…"), this);
    browse->setFixedWidth(32);
    auto* notesRow = new QWidget(this);
    auto* notesLayout = new QHBoxLayout(notesRow);
    notesLayout->setContentsMargins(0, 0, 0, 0);
    notesLayout->addWidget(notes, 1);
    notesLayout->addWidget(browse);

    // Vault. The whole feature hangs off this one checkbox: unchecked, loom
    // behaves exactly as it did before vaults existed, and the folder row is
    // disabled so it is obvious the path is inert rather than merely unset.
    auto* vaultEnabled = new QCheckBox(QStringLiteral("enable vault"), this);
    vaultEnabled->setChecked(settings.vaultEnabled);
    auto* vaultRoot = new QLineEdit(settings.vaultRoot, this);
    vaultRoot->setPlaceholderText(QStringLiteral("folder to use as the vault"));
    auto* vaultBrowse = new QPushButton(QStringLiteral("…"), this);
    vaultBrowse->setFixedWidth(32);
    auto* vaultRow = new QWidget(this);
    auto* vaultLayout = new QHBoxLayout(vaultRow);
    vaultLayout->setContentsMargins(0, 0, 0, 0);
    vaultLayout->addWidget(vaultRoot, 1);
    vaultLayout->addWidget(vaultBrowse);
    auto* vaultSidebar = new QCheckBox(QStringLiteral("show vault tree (ctrl+e)"), this);
    vaultSidebar->setChecked(settings.vaultSidebarVisible);

    auto syncVaultRow = [vaultRow, vaultSidebar](bool on) {
        vaultRow->setEnabled(on);
        vaultSidebar->setEnabled(on);
    };
    syncVaultRow(settings.vaultEnabled);
    connect(vaultEnabled, &QCheckBox::toggled, this, syncVaultRow);

    form->addRow(QStringLiteral("body font"), bodyFont);
    form->addRow(QStringLiteral("chrome font"), chromeFont);
    form->addRow(QStringLiteral("size"), size);
    form->addRow(QStringLiteral("line height"), lineHeight);
    form->addRow(QStringLiteral("theme"), theme);
    form->addRow(QStringLiteral("scanlines"), scan);
    form->addRow(blockCaret);
    form->addRow(crt);
    form->addRow(autosave);
    form->addRow(zen);
    form->addRow(QStringLiteral("notes dir"), notesRow);
    form->addRow(vaultEnabled);
    form->addRow(QStringLiteral("vault dir"), vaultRow);
    form->addRow(vaultSidebar);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    auto* root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this, notes]() {
        const QString dir =
            ThemedDialogs::getExistingDirectory(this, QStringLiteral("notes directory"), notes->text());
        if (!dir.isEmpty()) {
            notes->setText(dir);
        }
    });

    connect(vaultBrowse, &QPushButton::clicked, this, [this, vaultRoot]() {
        const QString dir = ThemedDialogs::getExistingDirectory(
            this, QStringLiteral("vault directory"), vaultRoot->text());
        if (!dir.isEmpty()) {
            vaultRoot->setText(dir);
        }
    });

    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, bodyFont, chromeFont, size, lineHeight, theme, scan, blockCaret, crt, autosave,
             zen, notes, vaultEnabled, vaultRoot, vaultSidebar]() {
                result_.bodyFont = bodyFont->currentText();
                result_.chromeFont = chromeFont->currentText();
                result_.bodyPointSize = size->value();
                result_.lineHeight = lineHeight->value();
                result_.themeSource = theme->currentData().toString();
                result_.scanlineIntensity = scan->value() / 100.0;
                result_.blockCaret = blockCaret->isChecked();
                result_.crtWipe = crt->isChecked();
                result_.autosaveNamedFiles = autosave->isChecked();
                result_.zenByDefault = zen->isChecked();
                result_.notesDirectory = notes->text();
                result_.vaultRoot = vaultRoot->text().trimmed();
                // Ticking the box without a folder would leave the feature "on"
                // but pointing nowhere, so it only counts as enabled once there
                // is a path to enable.
                result_.vaultEnabled = vaultEnabled->isChecked() && !result_.vaultRoot.isEmpty();
                result_.vaultSidebarVisible = vaultSidebar->isChecked();
                accept();
            });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setFont(Fonts::chrome(settings, 10));
}
