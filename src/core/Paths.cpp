#include "core/Paths.h"

#include <QDir>
#include <QStandardPaths>

QString Paths::stateDir() {
    const QString env = qEnvironmentVariable("LOOM_STATE_DIR");
    if (!env.isEmpty()) {
        return env;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/loom");
}

QString Paths::configDir() {
    const QString env = qEnvironmentVariable("LOOM_CONFIG_DIR");
    if (!env.isEmpty()) {
        return env;
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/loom");
}

QString Paths::configFile() {
    return configDir() + QStringLiteral("/config.toml");
}

QString Paths::sessionFile() {
    return stateDir() + QStringLiteral("/session.json");
}

QString Paths::scratchDir() {
    return stateDir() + QStringLiteral("/scratch");
}

QString Paths::scratchFile(const QString& id) {
    return scratchDir() + QLatin1Char('/') + id + QStringLiteral(".md");
}

QString Paths::mediaDir() {
    return stateDir() + QStringLiteral("/media");
}

QString Paths::omarchyThemeFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/omarchy/current/theme/colors.toml");
}

QString Paths::omarchyThemeNameFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
        + QStringLiteral("/omarchy/current/theme.name");
}

void Paths::ensureDirectories() {
    QDir().mkpath(stateDir());
    QDir().mkpath(configDir());
    QDir().mkpath(scratchDir());
    QDir().mkpath(mediaDir());
}
