#pragma once

#include <QString>
#include <QWidget>

namespace ThemedDialogs {
QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir,
                        const QString& filter);
QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir,
                        const QString& filter);
QString getExistingDirectory(QWidget* parent, const QString& caption, const QString& dir);
}
