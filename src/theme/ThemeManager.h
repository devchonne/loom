#pragma once

#include "theme/Theme.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QPalette>

struct Settings;

class ThemeManager : public QObject {
    Q_OBJECT

public:
    explicit ThemeManager(QObject* parent = nullptr);

    const Theme& theme() const { return theme_; }
    void apply(const Settings& settings);
    QPalette palette() const;
    QString styleSheet() const;

signals:
    void themeChanged();

private:
    void reload();
    void watchOmarchy();

    Theme theme_ = Theme::builtin();
    QString source_ = QStringLiteral("omarchy");
    QString customPath_;
    QFileSystemWatcher watcher_;
};
