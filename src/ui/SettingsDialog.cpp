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
    resize(460, 420);

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

    connect(buttons, &QDialogButtonBox::accepted, this,
            [this, bodyFont, chromeFont, size, lineHeight, theme, scan, blockCaret, crt, autosave, zen, notes]() {
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
                accept();
            });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setFont(Fonts::chrome(settings, 10));
}
