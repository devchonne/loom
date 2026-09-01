#include "core/SlashCommand.h"

#include <gtest/gtest.h>

TEST(SlashCommand, ParsesNameAndArg) {
    const auto rain = parseSlashLine(QStringLiteral("  /rain 1  "));
    ASSERT_TRUE(rain.has_value());
    EXPECT_EQ(rain->name, QStringLiteral("rain"));
    EXPECT_EQ(rain->arg, QStringLiteral("1"));

    const auto save = parseSlashLine(QStringLiteral("/save"));
    ASSERT_TRUE(save.has_value());
    EXPECT_EQ(save->name, QStringLiteral("save"));
    EXPECT_TRUE(save->arg.isEmpty());

    const auto theme = parseSlashLine(QStringLiteral("/theme tokyo night"));
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(theme->name, QStringLiteral("theme"));
    EXPECT_EQ(theme->arg, QStringLiteral("tokyo night"));
}

TEST(SlashCommand, RejectsNonCommands) {
    EXPECT_FALSE(parseSlashLine(QStringLiteral("rain 1")).has_value());
    EXPECT_FALSE(parseSlashLine(QStringLiteral("// comment")).has_value());
    EXPECT_FALSE(parseSlashLine(QStringLiteral("/usr/bin")).has_value());
    EXPECT_FALSE(parseSlashLine(QStringLiteral("/  rain")).has_value());
    EXPECT_FALSE(parseSlashLine(QStringLiteral("")).has_value());
}

TEST(SlashCommand, OnOff) {
    EXPECT_EQ(parseOnOff(QString()), OnOff::Default);
    EXPECT_EQ(parseOnOff(QStringLiteral("1")), OnOff::On);
    EXPECT_EQ(parseOnOff(QStringLiteral("on")), OnOff::On);
    EXPECT_EQ(parseOnOff(QStringLiteral("0")), OnOff::Off);
    EXPECT_EQ(parseOnOff(QStringLiteral("OFF")), OnOff::Off);
    EXPECT_EQ(parseOnOff(QStringLiteral("maybe")), OnOff::Invalid);
}
