#pragma once

#include "markdown/MarkdownRules.h"
#include "theme/Theme.h"

#include <QSyntaxHighlighter>
#include <QTextBlockUserData>
#include <QTextCharFormat>
#include <QTextBlock>

struct MarkdownBlockData : public QTextBlockUserData {
    BlockKind kind = BlockKind::Paragraph;
    int headingLevel = 0;
    int fenceLine = 0;
    int markerStart = 0;
    QChar listMarker;
    int listLevel = 0;
    bool revealed = false;
    QString fenceLang;
    QString imagePath;
};

inline MarkdownBlockData* markdownData(const QTextBlock& block) {
    return static_cast<MarkdownBlockData*>(block.userData());
}

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QObject* parent = nullptr);

    void setTheme(const Theme& theme);
    void setBasePointSize(qreal size);
    void setBodyFamily(const QString& family);
    void setEnabled(bool enabled);
    void setRevealedBlock(int blockNumber);
    bool isEnabled() const { return enabled_; }
    int revealedBlock() const { return revealedBlock_; }

protected:
    void highlightBlock(const QString& text) override;

private:
    QTextCharFormat formatFor(SpanKind kind, int headingLevel) const;
    qreal headingSize(int level) const;

    Theme theme_ = Theme::builtin();
    qreal basePointSize_ = 13.5;
    QString bodyFamily_ = QStringLiteral("monospace");
    bool enabled_ = true;
    int revealedBlock_ = 0;
};
