#include "core/LineDiff.h"

#include <algorithm>
#include <vector>

LineDiffResult diffLines(const QStringList& left, const QStringList& right) {
    LineDiffResult out;
    const int n = left.size();
    const int m = right.size();
    out.left = QVector<LineMark>(n, LineMark::Equal);
    out.right = QVector<LineMark>(m, LineMark::Equal);

    if (n == 0) {
        out.right.fill(LineMark::Insert, m);
        return out;
    }
    if (m == 0) {
        out.left.fill(LineMark::Delete, n);
        return out;
    }

    const qint64 cells = static_cast<qint64>(n + 1) * (m + 1);
    if (cells > 2'000'000) {
        const int common = std::min(n, m);
        for (int i = 0; i < common; ++i) {
            if (left[i] != right[i]) {
                out.left[i] = LineMark::Change;
                out.right[i] = LineMark::Change;
            }
        }
        for (int i = common; i < n; ++i) {
            out.left[i] = LineMark::Delete;
        }
        for (int j = common; j < m; ++j) {
            out.right[j] = LineMark::Insert;
        }
        return out;
    }

    std::vector<int> dp(static_cast<size_t>(n + 1) * static_cast<size_t>(m + 1), 0);
    const int stride = m + 1;
    auto at = [stride](int i, int j) { return i * stride + j; };
    for (int i = n - 1; i >= 0; --i) {
        for (int j = m - 1; j >= 0; --j) {
            if (left[i] == right[j]) {
                dp[at(i, j)] = dp[at(i + 1, j + 1)] + 1;
            } else {
                dp[at(i, j)] = std::max(dp[at(i + 1, j)], dp[at(i, j + 1)]);
            }
        }
    }

    int i = 0;
    int j = 0;
    while (i < n || j < m) {
        if (i < n && j < m && left[i] == right[j]) {
            ++i;
            ++j;
            continue;
        }
        const int startI = i;
        const int startJ = j;
        while (i < n || j < m) {
            if (i < n && j < m && left[i] == right[j]) {
                break;
            }
            if (i < n && j < m) {
                if (dp[at(i + 1, j)] >= dp[at(i, j + 1)]) {
                    ++i;
                } else {
                    ++j;
                }
            } else if (i < n) {
                ++i;
            } else {
                ++j;
            }
        }
        const int nDel = i - startI;
        const int nIns = j - startJ;
        const int nChg = std::min(nDel, nIns);
        for (int k = 0; k < nChg; ++k) {
            out.left[startI + k] = LineMark::Change;
            out.right[startJ + k] = LineMark::Change;
        }
        for (int k = nChg; k < nDel; ++k) {
            out.left[startI + k] = LineMark::Delete;
        }
        for (int k = nChg; k < nIns; ++k) {
            out.right[startJ + k] = LineMark::Insert;
        }
    }
    return out;
}
