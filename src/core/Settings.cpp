#include "core/Settings.h"

#include "core/AtomicFile.h"
#include "core/Paths.h"

#include "theme/Theme.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <sstream>
#include <toml++/toml.hpp>

Settings Settings::load() {
    Settings s;
    Paths::ensureDirectories();
    const QString path = Paths::configFile();
    if (!QFileInfo::exists(path)) {
        return s;
    }

    QString err;
    const QByteArray bytes = AtomicFile::read(path, &err);
    if (bytes.isEmpty() && !err.isEmpty()) {
        return s;
    }

    try {
        const auto tbl = toml::parse(bytes.toStdString());
        s.bodyFont = QString::fromStdString(tbl["body_font"].value_or(s.bodyFont.toStdString()));
        s.chromeFont = QString::fromStdString(tbl["chrome_font"].value_or(s.chromeFont.toStdString()));
        s.bodyPointSize = tbl["body_point_size"].value_or(s.bodyPointSize);
        s.lineHeight = tbl["line_height"].value_or(s.lineHeight);
        s.themeSource =
            Palettes::normalize(QString::fromStdString(tbl["theme_source"].value_or(s.themeSource.toStdString())));
        s.customThemePath =
            QString::fromStdString(tbl["custom_theme_path"].value_or(s.customThemePath.toStdString()));
        s.scanlineIntensity = tbl["scanline_intensity"].value_or(s.scanlineIntensity);
        s.blockCaret = tbl["block_caret"].value_or(s.blockCaret);
        s.zenByDefault = tbl["zen_by_default"].value_or(s.zenByDefault);
        s.autosaveNamedFiles = tbl["autosave_named_files"].value_or(s.autosaveNamedFiles);
        s.crtWipe = tbl["crt_wipe"].value_or(s.crtWipe);
        s.keyclick = tbl["keyclick"].value_or(s.keyclick);
        s.notesDirectory =
            QString::fromStdString(tbl["notes_directory"].value_or(s.notesDirectory.toStdString()));
    } catch (const toml::parse_error&) {
        return s;
    }
    return s;
}

bool Settings::save(QString* error) const {
    toml::table tbl;
    tbl.insert("body_font", bodyFont.toStdString());
    tbl.insert("chrome_font", chromeFont.toStdString());
    tbl.insert("body_point_size", bodyPointSize);
    tbl.insert("line_height", lineHeight);
    tbl.insert("theme_source", themeSource.toStdString());
    tbl.insert("custom_theme_path", customThemePath.toStdString());
    tbl.insert("scanline_intensity", scanlineIntensity);
    tbl.insert("block_caret", blockCaret);
    tbl.insert("zen_by_default", zenByDefault);
    tbl.insert("autosave_named_files", autosaveNamedFiles);
    tbl.insert("crt_wipe", crtWipe);
    tbl.insert("keyclick", keyclick);
    tbl.insert("notes_directory", notesDirectory.toStdString());

    std::ostringstream ss;
    ss << tbl;
    return AtomicFile::write(Paths::configFile(), QByteArray::fromStdString(ss.str()), error);
}

QString Settings::resolvedNotesDirectory() const {
    if (!notesDirectory.isEmpty()) {
        return notesDirectory;
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        + QStringLiteral("/loom");
}
