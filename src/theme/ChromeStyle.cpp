#include "theme/ChromeStyle.h"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

namespace {

bool isChromeIcon(QStyle::StandardPixmap which) {
    switch (which) {
    case QStyle::SP_ArrowBack:
    case QStyle::SP_ArrowForward:
    case QStyle::SP_ArrowLeft:
    case QStyle::SP_ArrowRight:
    case QStyle::SP_ArrowUp:
    case QStyle::SP_ArrowDown:
    case QStyle::SP_FileDialogStart:
    case QStyle::SP_FileDialogEnd:
    case QStyle::SP_FileDialogToParent:
    case QStyle::SP_FileDialogNewFolder:
    case QStyle::SP_FileDialogListView:
    case QStyle::SP_FileDialogDetailedView:
    case QStyle::SP_FileDialogInfoView:
    case QStyle::SP_FileDialogContentsView:
    case QStyle::SP_DirHomeIcon:
    case QStyle::SP_DirIcon:
    case QStyle::SP_DirOpenIcon:
    case QStyle::SP_DirClosedIcon:
    case QStyle::SP_FileIcon:
    case QStyle::SP_ComputerIcon:
    case QStyle::SP_DesktopIcon:
    case QStyle::SP_DriveFDIcon:
    case QStyle::SP_DriveHDIcon:
    case QStyle::SP_DriveCDIcon:
    case QStyle::SP_DriveNetIcon:
    case QStyle::SP_DialogOkButton:
    case QStyle::SP_DialogCancelButton:
    case QStyle::SP_DialogHelpButton:
    case QStyle::SP_DialogOpenButton:
    case QStyle::SP_DialogSaveButton:
    case QStyle::SP_DialogCloseButton:
    case QStyle::SP_DialogApplyButton:
    case QStyle::SP_DialogResetButton:
    case QStyle::SP_DialogDiscardButton:
    case QStyle::SP_DialogYesButton:
    case QStyle::SP_DialogNoButton:
    case QStyle::SP_TrashIcon:
    case QStyle::SP_BrowserReload:
    case QStyle::SP_LineEditClearButton:
        return true;
    default:
        return false;
    }
}

QColor iconColor(QIcon::Mode mode) {
    const QPalette pal = QApplication::palette();
    if (mode == QIcon::Disabled) {
        return pal.color(QPalette::Disabled, QPalette::WindowText);
    }
    if (mode == QIcon::Selected) {
        return pal.color(QPalette::HighlightedText);
    }
    return pal.color(QPalette::WindowText);
}

void paintChromeIcon(QPainter* p, const QRect& rect, QStyle::StandardPixmap which, const QColor& color) {
    if (rect.width() < 4 || rect.height() < 4) {
        return;
    }
    p->save();
    p->setRenderHint(QPainter::Antialiasing, false);
    const int side = qMin(rect.width(), rect.height());
    p->translate(rect.x() + (rect.width() - side) / 2.0, rect.y() + (rect.height() - side) / 2.0);
    const qreal s = side / 16.0;
    p->scale(s, s);
    QPen pen(color, qMax(1.0, 1.15 / s));
    pen.setJoinStyle(Qt::MiterJoin);
    pen.setCapStyle(Qt::FlatCap);
    p->setPen(pen);
    p->setBrush(Qt::NoBrush);

    auto chevronL = [&]() { p->drawLine(QPointF(9, 4), QPointF(5, 8)); p->drawLine(QPointF(5, 8), QPointF(9, 12)); };
    auto chevronR = [&]() { p->drawLine(QPointF(7, 4), QPointF(11, 8)); p->drawLine(QPointF(11, 8), QPointF(7, 12)); };
    auto chevronU = [&]() { p->drawLine(QPointF(4, 9), QPointF(8, 5)); p->drawLine(QPointF(8, 5), QPointF(12, 9)); };
    auto chevronD = [&]() { p->drawLine(QPointF(4, 7), QPointF(8, 11)); p->drawLine(QPointF(8, 11), QPointF(12, 7)); };
    auto folder = [&]() {
        p->drawLine(QPointF(2, 5), QPointF(2, 4));
        p->drawLine(QPointF(2, 4), QPointF(6, 4));
        p->drawLine(QPointF(6, 4), QPointF(7, 6));
        p->drawRect(QRectF(2, 6, 12, 8));
    };
    auto file = [&]() {
        QPolygonF outline;
        outline << QPointF(4, 2) << QPointF(10, 2) << QPointF(12, 4) << QPointF(12, 14) << QPointF(4, 14)
                << QPointF(4, 2);
        p->drawPolyline(outline);
        p->drawLine(QPointF(10, 2), QPointF(10, 4));
        p->drawLine(QPointF(10, 4), QPointF(12, 4));
    };
    auto cross = [&]() {
        p->drawLine(QPointF(4, 4), QPointF(12, 12));
        p->drawLine(QPointF(12, 4), QPointF(4, 12));
    };
    auto check = [&]() {
        p->drawLine(QPointF(3, 8), QPointF(6, 12));
        p->drawLine(QPointF(6, 12), QPointF(13, 4));
    };

    switch (which) {
    case QStyle::SP_ArrowBack:
    case QStyle::SP_ArrowLeft:
    case QStyle::SP_FileDialogStart:
        chevronL();
        break;
    case QStyle::SP_ArrowForward:
    case QStyle::SP_ArrowRight:
    case QStyle::SP_FileDialogEnd:
        chevronR();
        break;
    case QStyle::SP_ArrowUp:
    case QStyle::SP_FileDialogToParent:
        chevronU();
        break;
    case QStyle::SP_ArrowDown:
        chevronD();
        break;
    case QStyle::SP_DirIcon:
    case QStyle::SP_DirOpenIcon:
    case QStyle::SP_DirClosedIcon:
    case QStyle::SP_DialogOpenButton:
        folder();
        break;
    case QStyle::SP_DirHomeIcon:
        p->drawLine(QPointF(3, 8), QPointF(8, 3));
        p->drawLine(QPointF(8, 3), QPointF(13, 8));
        p->drawRect(QRectF(5, 8, 6, 5));
        break;
    case QStyle::SP_FileDialogNewFolder:
        folder();
        p->drawLine(QPointF(11, 8), QPointF(11, 12));
        p->drawLine(QPointF(9, 10), QPointF(13, 10));
        break;
    case QStyle::SP_FileIcon:
    case QStyle::SP_DialogSaveButton:
        file();
        break;
    case QStyle::SP_ComputerIcon:
    case QStyle::SP_DesktopIcon:
        p->drawRect(QRectF(3, 3, 10, 7));
        p->drawLine(QPointF(8, 10), QPointF(8, 12));
        p->drawLine(QPointF(5, 13), QPointF(11, 13));
        break;
    case QStyle::SP_DriveFDIcon:
    case QStyle::SP_DriveHDIcon:
    case QStyle::SP_DriveCDIcon:
        p->drawRect(QRectF(2, 5, 12, 7));
        p->drawLine(QPointF(4, 8), QPointF(7, 8));
        break;
    case QStyle::SP_DriveNetIcon:
        p->drawRect(QRectF(2, 3, 4, 4));
        p->drawRect(QRectF(10, 9, 4, 4));
        p->drawLine(QPointF(6, 5), QPointF(10, 11));
        break;
    case QStyle::SP_FileDialogListView:
        p->drawLine(QPointF(3, 4), QPointF(13, 4));
        p->drawLine(QPointF(3, 8), QPointF(13, 8));
        p->drawLine(QPointF(3, 12), QPointF(13, 12));
        break;
    case QStyle::SP_FileDialogDetailedView:
    case QStyle::SP_FileDialogInfoView:
        p->drawRect(QRectF(2, 3, 3, 3));
        p->drawLine(QPointF(7, 4.5), QPointF(14, 4.5));
        p->drawRect(QRectF(2, 8, 3, 3));
        p->drawLine(QPointF(7, 9.5), QPointF(14, 9.5));
        break;
    case QStyle::SP_FileDialogContentsView:
        p->drawRect(QRectF(3, 3, 4, 4));
        p->drawRect(QRectF(9, 3, 4, 4));
        p->drawRect(QRectF(3, 9, 4, 4));
        p->drawRect(QRectF(9, 9, 4, 4));
        break;
    case QStyle::SP_DialogCancelButton:
    case QStyle::SP_DialogCloseButton:
    case QStyle::SP_DialogNoButton:
    case QStyle::SP_DialogDiscardButton:
    case QStyle::SP_LineEditClearButton:
        cross();
        break;
    case QStyle::SP_DialogOkButton:
    case QStyle::SP_DialogYesButton:
    case QStyle::SP_DialogApplyButton:
        check();
        break;
    case QStyle::SP_DialogHelpButton:
        p->drawRect(QRectF(6, 2, 4, 4));
        p->drawLine(QPointF(8, 6), QPointF(8, 10));
        p->drawPoint(QPointF(8, 13));
        break;
    case QStyle::SP_DialogResetButton:
    case QStyle::SP_BrowserReload:
        p->drawArc(QRectF(3, 3, 10, 10), 40 * 16, 280 * 16);
        p->drawLine(QPointF(12, 3), QPointF(12, 7));
        p->drawLine(QPointF(12, 3), QPointF(8, 4));
        break;
    case QStyle::SP_TrashIcon:
        p->drawLine(QPointF(5, 4), QPointF(11, 4));
        p->drawLine(QPointF(4, 6), QPointF(12, 6));
        p->drawLine(QPointF(5, 6), QPointF(6, 13));
        p->drawLine(QPointF(11, 6), QPointF(10, 13));
        p->drawLine(QPointF(6, 13), QPointF(10, 13));
        break;
    default:
        break;
    }
    p->restore();
}

class ChromeIconEngine : public QIconEngine {
public:
    explicit ChromeIconEngine(QStyle::StandardPixmap which)
        : which_(which) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        paintChromeIcon(painter, rect, which_, iconColor(mode));
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
        QPixmap pm(size * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        paint(&p, QRect(QPoint(), size), mode, state);
        return pm;
    }

    QIconEngine* clone() const override { return new ChromeIconEngine(which_); }

private:
    QStyle::StandardPixmap which_;
};

QStyle::StandardPixmap iconTypeToPixmap(QFileIconProvider::IconType type) {
    switch (type) {
    case QFileIconProvider::Computer:
        return QStyle::SP_ComputerIcon;
    case QFileIconProvider::Desktop:
        return QStyle::SP_DesktopIcon;
    case QFileIconProvider::Trashcan:
        return QStyle::SP_TrashIcon;
    case QFileIconProvider::Network:
        return QStyle::SP_DriveNetIcon;
    case QFileIconProvider::Drive:
        return QStyle::SP_DriveHDIcon;
    case QFileIconProvider::Folder:
        return QStyle::SP_DirIcon;
    case QFileIconProvider::File:
    default:
        return QStyle::SP_FileIcon;
    }
}

} // namespace

ChromeStyle::ChromeStyle(QStyle* base)
    : QProxyStyle(base) {}

QIcon ChromeStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption* option,
                                const QWidget* widget) const {
    if (isChromeIcon(standardIcon)) {
        return QIcon(new ChromeIconEngine(standardIcon));
    }
    return QProxyStyle::standardIcon(standardIcon, option, widget);
}

QPixmap ChromeStyle::standardPixmap(StandardPixmap standardPixmap, const QStyleOption* option,
                                    const QWidget* widget) const {
    if (isChromeIcon(standardPixmap)) {
        return standardIcon(standardPixmap, option, widget).pixmap(16, 16);
    }
    return QProxyStyle::standardPixmap(standardPixmap, option, widget);
}

QIcon ChromeIconProvider::icon(IconType type) const {
    return QIcon(new ChromeIconEngine(iconTypeToPixmap(type)));
}

QIcon ChromeIconProvider::icon(const QFileInfo& info) const {
    if (info.isRoot()) {
        return icon(Computer);
    }
    if (info.isDir()) {
        return icon(Folder);
    }
    return icon(File);
}

QFileIconProvider* chromeIconProvider() {
    static ChromeIconProvider provider;
    return &provider;
}
