#pragma once

#include <QString>
#include <optional>

struct SlashCommand {
    QString name;
    QString arg;
};

enum class OnOff { On, Off, Default, Invalid };

std::optional<SlashCommand> parseSlashLine(const QString& line);
OnOff parseOnOff(const QString& arg);
