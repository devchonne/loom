#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct Theme {
    QColor background;
    QColor darkBackground;
    QColor darkerBackground;
    QColor lighterBackground;
    QColor foreground;
    QColor darkForeground;
    QColor lightForeground;
    QColor brightForeground;
    QColor accent;
    QColor selection;
    QColor muted;
    QColor red;
    QColor yellow;
    QColor orange;
    QColor green;
    QColor cyan;
    QColor blue;
    QColor magenta;
    QColor brown;
    bool dark = true;

    static Theme builtin();
};

struct ThemeSpec {
    QString id;
    QString name;
    QString group;
};

namespace Palettes {
QVector<ThemeSpec> catalog();
Theme byId(const QString& id);
QString normalize(const QString& id);
QString displayName(const QString& id);
QString cycle(const QString& id, int delta);
bool isOmarchy(const QString& id);
}

class OmarchyThemeSource {
public:
    static Theme parse(const QString& tomlText, QString* error = nullptr);
    static Theme fromFile(const QString& path, QString* error = nullptr);
};
