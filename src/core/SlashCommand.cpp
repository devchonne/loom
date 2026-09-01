#include "core/SlashCommand.h"

std::optional<SlashCommand> parseSlashLine(const QString& line) {
    const QString t = line.trimmed();
    if (t.size() < 2 || t.front() != QLatin1Char('/')) {
        return std::nullopt;
    }
    int i = 1;
    if (!t.at(i).isLetter()) {
        return std::nullopt;
    }
    while (i < t.size() && (t.at(i).isLetterOrNumber() || t.at(i) == QLatin1Char('-'))) {
        ++i;
    }
    if (i < t.size() && !t.at(i).isSpace()) {
        return std::nullopt;
    }
    SlashCommand cmd;
    cmd.name = t.mid(1, i - 1).toLower();
    cmd.arg = t.mid(i).trimmed();
    return cmd;
}

OnOff parseOnOff(const QString& arg) {
    if (arg.isEmpty()) {
        return OnOff::Default;
    }
    const QString a = arg.toLower();
    if (a == QLatin1String("1") || a == QLatin1String("on") || a == QLatin1String("true")
        || a == QLatin1String("yes")) {
        return OnOff::On;
    }
    if (a == QLatin1String("0") || a == QLatin1String("off") || a == QLatin1String("false")
        || a == QLatin1String("no")) {
        return OnOff::Off;
    }
    return OnOff::Invalid;
}
