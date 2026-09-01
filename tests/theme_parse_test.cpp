#include "theme/Theme.h"

#include <QStringList>
#include <gtest/gtest.h>

TEST(ThemeParse, OmarchyColorsToml) {
    const QString toml = QStringLiteral(R"(
mode = "dark"
accent = "#7aa2f7"
selection = "#292e42"
muted = "#414868"
background = "#1a1b26"
dark_background = "#13141c"
darker_background = "#0e0e14"
lighter_background = "#24283b"
foreground = "#a9b1d6"
dark_foreground = "#565f89"
light_foreground = "#b4bee6"
bright_foreground = "#c0caf5"
red = "#f7768e"
yellow = "#e0af68"
green = "#9ece6a"
cyan = "#449dab"
blue = "#7aa2f7"
magenta = "#ad8ee6"
)");
    QString err;
    const Theme theme = OmarchyThemeSource::parse(toml, &err);
    EXPECT_TRUE(err.isEmpty()) << err.toStdString();
    EXPECT_TRUE(theme.dark);
    EXPECT_EQ(theme.accent.name(), QStringLiteral("#7aa2f7"));
    EXPECT_EQ(theme.background.name(), QStringLiteral("#1a1b26"));
    EXPECT_EQ(theme.green.name(), QStringLiteral("#9ece6a"));
    EXPECT_EQ(theme.foreground.name(), QStringLiteral("#a9b1d6"));
}

TEST(ThemeParse, InvalidFallsBack) {
    QString err;
    const Theme theme = OmarchyThemeSource::parse(QStringLiteral("??? not toml"), &err);
    EXPECT_FALSE(err.isEmpty());
    EXPECT_EQ(theme.background.name(), Theme::builtin().background.name());
}

TEST(ThemeParse, LightMode) {
    const Theme theme = OmarchyThemeSource::parse(QStringLiteral("mode = \"light\"\nbackground = \"#fff1e0\"\n"));
    EXPECT_FALSE(theme.dark);
    EXPECT_EQ(theme.background.name(), QStringLiteral("#fff1e0"));
}

TEST(Palettes, CatalogHasHackerAndChill) {
    const auto themes = Palettes::catalog();
    QStringList ids;
    QStringList groups;
    for (const ThemeSpec& spec : themes) {
        ids.push_back(spec.id);
        if (!groups.contains(spec.group)) {
            groups.push_back(spec.group);
        }
    }
    EXPECT_TRUE(ids.contains(QStringLiteral("omarchy")));
    EXPECT_TRUE(ids.contains(QStringLiteral("phosphor")));
    EXPECT_TRUE(ids.contains(QStringLiteral("amber")));
    EXPECT_TRUE(ids.contains(QStringLiteral("hotline")));
    EXPECT_TRUE(ids.contains(QStringLiteral("rootkit")));
    EXPECT_TRUE(ids.contains(QStringLiteral("soviet")));
    EXPECT_TRUE(ids.contains(QStringLiteral("tokyo-night")));
    EXPECT_TRUE(ids.contains(QStringLiteral("nord")));
    EXPECT_TRUE(ids.contains(QStringLiteral("matcha")));
    EXPECT_TRUE(ids.contains(QStringLiteral("dusk")));
    EXPECT_TRUE(ids.contains(QStringLiteral("paper")));
    EXPECT_TRUE(groups.contains(QStringLiteral("hacker")));
    EXPECT_TRUE(groups.contains(QStringLiteral("chill")));
}

TEST(Palettes, BuiltinAliasAndCycle) {
    EXPECT_EQ(Palettes::normalize(QStringLiteral("builtin")), QStringLiteral("tokyo-night"));
    EXPECT_EQ(Theme::builtin().background.name(), Palettes::byId(QStringLiteral("tokyo-night")).background.name());
    EXPECT_TRUE(Palettes::byId(QStringLiteral("phosphor")).dark);
    EXPECT_TRUE(Palettes::byId(QStringLiteral("soviet")).dark);
    EXPECT_FALSE(Palettes::byId(QStringLiteral("paper")).dark);
    EXPECT_EQ(Palettes::byId(QStringLiteral("phosphor")).accent.name(), QStringLiteral("#33ff66"));
    EXPECT_EQ(Palettes::byId(QStringLiteral("soviet")).accent.name(), QStringLiteral("#e03528"));
    EXPECT_EQ(Palettes::cycle(QStringLiteral("omarchy"), 1), QStringLiteral("phosphor"));
    EXPECT_EQ(Palettes::cycle(QStringLiteral("paper"), 1), QStringLiteral("omarchy"));
    EXPECT_EQ(Palettes::displayName(QStringLiteral("amber")), QStringLiteral("amber crt"));
}
