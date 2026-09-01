#pragma once

#include <QByteArray>
#include <QString>

class AtomicFile {
public:
    static bool write(const QString& path, const QByteArray& data, QString* error = nullptr);
    static QByteArray read(const QString& path, QString* error = nullptr);
};
