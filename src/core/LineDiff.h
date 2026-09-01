#pragma once

#include <QStringList>
#include <QVector>
#include <cstdint>

enum class LineMark : std::uint8_t {
    Equal,
    Insert,
    Delete,
    Change,
};

struct LineDiffResult {
    QVector<LineMark> left;
    QVector<LineMark> right;
};

LineDiffResult diffLines(const QStringList& left, const QStringList& right);
