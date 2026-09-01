#pragma once

#include <QFileIconProvider>
#include <QProxyStyle>

class ChromeStyle : public QProxyStyle {
public:
    explicit ChromeStyle(QStyle* base);

    QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption* option = nullptr,
                       const QWidget* widget = nullptr) const override;
    QPixmap standardPixmap(StandardPixmap standardPixmap, const QStyleOption* option = nullptr,
                           const QWidget* widget = nullptr) const override;
};

class ChromeIconProvider : public QFileIconProvider {
public:
    QIcon icon(IconType type) const override;
    QIcon icon(const QFileInfo& info) const override;
};

QFileIconProvider* chromeIconProvider();
