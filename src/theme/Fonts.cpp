#include "theme/Fonts.h"

#include "core/Settings.h"

#include <QFontDatabase>

void Fonts::loadEmbedded() {
    QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/DepartureMono-Regular.otf"));
}

QString Fonts::resolve(const QStringList& candidates) {
    const QStringList families = QFontDatabase::families();
    for (const QString& name : candidates) {
        if (families.contains(name, Qt::CaseInsensitive)) {
            for (const QString& family : families) {
                if (family.compare(name, Qt::CaseInsensitive) == 0) {
                    return family;
                }
            }
        }
        for (const QString& family : families) {
            if (family.startsWith(name, Qt::CaseInsensitive)) {
                return family;
            }
        }
    }
    return QStringLiteral("monospace");
}

QFont Fonts::body(const Settings& settings, qreal pointSize) {
    const QString family = resolve({
        settings.bodyFont,
        QStringLiteral("Departure Mono"),
        QStringLiteral("DepartureMono"),
        QStringLiteral("iA Writer Mono S"),
        QStringLiteral("JetBrains Mono"),
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("Noto Sans Mono"),
    });
    QFont font(family);
    font.setPointSizeF(pointSize > 0 ? pointSize : settings.bodyPointSize);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

QFont Fonts::chrome(const Settings& settings, qreal pointSize) {
    const QString family = resolve({
        settings.chromeFont,
        QStringLiteral("Departure Mono"),
        QStringLiteral("DepartureMono"),
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("JetBrains Mono"),
        QStringLiteral("Noto Sans Mono"),
    });
    QFont font(family);
    font.setPointSizeF(pointSize > 0 ? pointSize : 10.0);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}
