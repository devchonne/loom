#pragma once

#include <QFont>
#include <QString>
#include <QStringList>

struct Settings;

class Fonts {
public:
    static void loadEmbedded();
    static QFont body(const Settings& settings, qreal pointSize = -1);
    static QFont chrome(const Settings& settings, qreal pointSize = -1);
    static QString resolve(const QStringList& candidates);
};
