#include "theme/Theme.h"

#include "core/AtomicFile.h"

#include <toml++/toml.hpp>

static QColor parseColor(const toml::table& table, const char* key, const QColor& fallback) {
    const auto value = table[key].value<std::string>();
    if (!value) {
        return fallback;
    }
    const QColor color(QString::fromStdString(*value));
    return color.isValid() ? color : fallback;
}

Theme OmarchyThemeSource::parse(const QString& tomlText, QString* error) {
    Theme theme = Theme::builtin();
    try {
        const auto table = toml::parse(tomlText.toStdString());
        const auto mode = table["mode"].value_or(std::string("dark"));
        theme.dark = (mode != "light");
        theme.accent = parseColor(table, "accent", theme.accent);
        theme.selection = parseColor(table, "selection", theme.selection);
        theme.muted = parseColor(table, "muted", theme.muted);
        theme.background = parseColor(table, "background", theme.background);
        theme.darkBackground = parseColor(table, "dark_background", theme.darkBackground);
        theme.darkerBackground = parseColor(table, "darker_background", theme.darkerBackground);
        theme.lighterBackground = parseColor(table, "lighter_background", theme.lighterBackground);
        theme.foreground = parseColor(table, "foreground", theme.foreground);
        theme.darkForeground = parseColor(table, "dark_foreground", theme.darkForeground);
        theme.lightForeground = parseColor(table, "light_foreground", theme.lightForeground);
        theme.brightForeground = parseColor(table, "bright_foreground", theme.brightForeground);
        theme.red = parseColor(table, "red", theme.red);
        theme.yellow = parseColor(table, "yellow", theme.yellow);
        theme.orange = parseColor(table, "orange", theme.orange);
        theme.green = parseColor(table, "green", theme.green);
        theme.cyan = parseColor(table, "cyan", theme.cyan);
        theme.blue = parseColor(table, "blue", theme.blue);
        theme.magenta = parseColor(table, "magenta", theme.magenta);
        theme.brown = parseColor(table, "brown", theme.brown);
    } catch (const toml::parse_error& e) {
        if (error) {
            *error = QString::fromUtf8(e.what());
        }
        return Theme::builtin();
    }
    return theme;
}

Theme OmarchyThemeSource::fromFile(const QString& path, QString* error) {
    const QByteArray bytes = AtomicFile::read(path, error);
    if (bytes.isEmpty()) {
        return Theme::builtin();
    }
    return parse(QString::fromUtf8(bytes), error);
}
