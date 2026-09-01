#include "core/AtomicFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <unistd.h>

bool AtomicFile::write(const QString& path, const QByteArray& data, QString* error) {
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error) {
            *error = QStringLiteral("could not create directory %1").arg(info.absolutePath());
        }
        return false;
    }

    const QString tmp = path + QStringLiteral(".tmp");
    QFile file(tmp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("could not open %1: %2").arg(tmp, file.errorString());
        }
        return false;
    }

    const qint64 written = file.write(data);
    if (written != data.size()) {
        if (error) {
            *error = QStringLiteral("short write to %1").arg(tmp);
        }
        file.close();
        QFile::remove(tmp);
        return false;
    }

    file.flush();
    if (::fsync(file.handle()) != 0) {
        if (error) {
            *error = QStringLiteral("fsync failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        file.close();
        QFile::remove(tmp);
        return false;
    }
    file.close();

    const QByteArray from = QFile::encodeName(tmp);
    const QByteArray to = QFile::encodeName(path);
    if (::rename(from.constData(), to.constData()) != 0) {
        if (error) {
            *error = QStringLiteral("rename failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        QFile::remove(tmp);
        return false;
    }
    return true;
}

QByteArray AtomicFile::read(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return {};
    }
    return file.readAll();
}
