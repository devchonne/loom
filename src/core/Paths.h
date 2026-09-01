#pragma once

#include <QString>

namespace Paths {
QString stateDir();
QString configDir();
QString configFile();
QString sessionFile();
QString scratchDir();
QString scratchFile(const QString& id);
QString mediaDir();
QString omarchyThemeFile();
QString omarchyThemeNameFile();
void ensureDirectories();
}
