#include "theme/Theme.h"

static Theme swatch(bool dark, const char* bg, const char* darkBg, const char* darkerBg, const char* lightBg,
                    const char* fg, const char* darkFg, const char* lightFg, const char* brightFg,
                    const char* accent, const char* selection, const char* muted, const char* red,
                    const char* yellow, const char* orange, const char* green, const char* cyan,
                    const char* blue, const char* magenta, const char* brown) {
    Theme t;
    t.dark = dark;
    t.background = QColor(QLatin1String(bg));
    t.darkBackground = QColor(QLatin1String(darkBg));
    t.darkerBackground = QColor(QLatin1String(darkerBg));
    t.lighterBackground = QColor(QLatin1String(lightBg));
    t.foreground = QColor(QLatin1String(fg));
    t.darkForeground = QColor(QLatin1String(darkFg));
    t.lightForeground = QColor(QLatin1String(lightFg));
    t.brightForeground = QColor(QLatin1String(brightFg));
    t.accent = QColor(QLatin1String(accent));
    t.selection = QColor(QLatin1String(selection));
    t.muted = QColor(QLatin1String(muted));
    t.red = QColor(QLatin1String(red));
    t.yellow = QColor(QLatin1String(yellow));
    t.orange = QColor(QLatin1String(orange));
    t.green = QColor(QLatin1String(green));
    t.cyan = QColor(QLatin1String(cyan));
    t.blue = QColor(QLatin1String(blue));
    t.magenta = QColor(QLatin1String(magenta));
    t.brown = QColor(QLatin1String(brown));
    return t;
}

Theme Theme::builtin() {
    return Palettes::byId(QStringLiteral("tokyo-night"));
}

QVector<ThemeSpec> Palettes::catalog() {
    return {
        {QStringLiteral("omarchy"), QStringLiteral("omarchy (live)"), QStringLiteral("system")},
        {QStringLiteral("phosphor"), QStringLiteral("phosphor"), QStringLiteral("hacker")},
        {QStringLiteral("amber"), QStringLiteral("amber crt"), QStringLiteral("hacker")},
        {QStringLiteral("hotline"), QStringLiteral("hotline"), QStringLiteral("hacker")},
        {QStringLiteral("rootkit"), QStringLiteral("rootkit"), QStringLiteral("hacker")},
        {QStringLiteral("soviet"), QStringLiteral("soviet"), QStringLiteral("hacker")},
        {QStringLiteral("tokyo-night"), QStringLiteral("tokyo night"), QStringLiteral("chill")},
        {QStringLiteral("nord"), QStringLiteral("nord mist"), QStringLiteral("chill")},
        {QStringLiteral("matcha"), QStringLiteral("matcha"), QStringLiteral("chill")},
        {QStringLiteral("dusk"), QStringLiteral("dusk"), QStringLiteral("chill")},
        {QStringLiteral("paper"), QStringLiteral("paper lantern"), QStringLiteral("chill")},
    };
}

QString Palettes::normalize(const QString& id) {
    if (id.isEmpty() || id == QLatin1String("builtin")) {
        return QStringLiteral("tokyo-night");
    }
    return id;
}

bool Palettes::isOmarchy(const QString& id) {
    return normalize(id) == QLatin1String("omarchy");
}

QString Palettes::displayName(const QString& id) {
    const QString key = normalize(id);
    for (const ThemeSpec& spec : catalog()) {
        if (spec.id == key) {
            return spec.name;
        }
    }
    return key;
}

Theme Palettes::byId(const QString& id) {
    const QString key = normalize(id);
    if (key == QLatin1String("phosphor")) {
        return swatch(true, "#030805", "#010302", "#000000", "#0c1610", "#8fd98f", "#2d5a32",
                      "#b6f0b6", "#dcffdc", "#33ff66", "#143314", "#1a3d22", "#ff5a5a", "#c8ff4a",
                      "#9dff6a", "#33ff66", "#66ffcc", "#4ad98f", "#a0ff78", "#3d5c2a");
    }
    if (key == QLatin1String("amber")) {
        return swatch(true, "#120b04", "#0a0602", "#050300", "#1e1408", "#e8b86d", "#7a5428",
                      "#f0c98a", "#ffdca0", "#ffb000", "#3a2810", "#5c4018", "#ff6a3c", "#ffc14a",
                      "#ff9a3c", "#c8a040", "#e0a050", "#d4923c", "#e09060", "#8a5a28");
    }
    if (key == QLatin1String("hotline")) {
        return swatch(true, "#0d0221", "#080116", "#050010", "#1a0a36", "#e8e0ff", "#6b5a8a",
                      "#f0e8ff", "#fff8ff", "#ff2bd6", "#3a1558", "#4a2a6a", "#ff3864", "#f9f002",
                      "#ff6b35", "#05ffa1", "#00f0ff", "#7b61ff", "#ff2bd6", "#8a4060");
    }
    if (key == QLatin1String("rootkit")) {
        return swatch(true, "#0b0c16", "#080910", "#06060c", "#151828", "#ddf7ff", "#6a6e95",
                      "#b5c5db", "#ddf7ff", "#82fb9c", "#1f253a", "#2d3450", "#50f872", "#50f7d4",
                      "#50f7a3", "#4fe88f", "#7cf8f7", "#829dd4", "#86a7df", "#287b51");
    }
    if (key == QLatin1String("soviet")) {
        return swatch(true, "#14090b", "#0c0507", "#070304", "#281116", "#ead6c4", "#8a645c",
                      "#f2e4d4", "#fff1e4", "#e03528", "#3a1418", "#6a3c40", "#ff5a4a", "#e0c04a",
                      "#d47832", "#7a9a52", "#7a9aa0", "#4a6a8c", "#c45a68", "#8a5a3a");
    }
    if (key == QLatin1String("nord")) {
        return swatch(true, "#2e3440", "#242933", "#1c2027", "#3b4252", "#d8dee9", "#616e88",
                      "#e5e9f0", "#eceff4", "#88c0d0", "#434c5e", "#4c566a", "#bf616a", "#ebcb8b",
                      "#d08770", "#a3be8c", "#8fbcbb", "#81a1c1", "#b48ead", "#8a7060");
    }
    if (key == QLatin1String("matcha")) {
        return swatch(true, "#2d353b", "#21272c", "#181d20", "#343f44", "#d3c6aa", "#7a8478",
                      "#e0d4b8", "#f0e6cc", "#a7c080", "#3d484d", "#4f585e", "#e67e80", "#dbbc7f",
                      "#e09d7f", "#a7c080", "#83c092", "#7fbbb3", "#d699b6", "#704e3f");
    }
    if (key == QLatin1String("dusk")) {
        return swatch(true, "#191724", "#12101a", "#0c0b12", "#26233a", "#e0def4", "#6e6a86",
                      "#f0eef8", "#faf8ff", "#c4a7e7", "#2a273f", "#403d52", "#eb6f92", "#f6c177",
                      "#ebbcba", "#9ccfd8", "#31748f", "#9ccfd8", "#c4a7e7", "#6b4f4a");
    }
    if (key == QLatin1String("paper")) {
        return swatch(false, "#f4ead8", "#e8dcc4", "#dccfb4", "#faf4e8", "#3d3229", "#8a7a68",
                      "#2a221c", "#1a1410", "#c45c26", "#e0c9a0", "#c4b49a", "#b54040", "#b8892a",
                      "#c45c26", "#5a7a48", "#4a7a72", "#4a6280", "#8a5a72", "#7a5a40");
    }
    return swatch(true, "#1a1b26", "#13141c", "#0e0e14", "#24283b", "#a9b1d6", "#565f89", "#b4bee6",
                  "#c0caf5", "#7aa2f7", "#292e42", "#414868", "#f7768e", "#e0af68", "#eb927b",
                  "#9ece6a", "#449dab", "#7aa2f7", "#ad8ee6", "#75493d");
}

QString Palettes::cycle(const QString& id, int delta) {
    const auto themes = catalog();
    if (themes.isEmpty()) {
        return QStringLiteral("tokyo-night");
    }
    const QString key = normalize(id);
    int index = 0;
    for (int i = 0; i < themes.size(); ++i) {
        if (themes[i].id == key) {
            index = i;
            break;
        }
    }
    const int n = themes.size();
    index = ((index + delta) % n + n) % n;
    return themes[index].id;
}
