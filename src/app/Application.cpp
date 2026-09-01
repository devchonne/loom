#include "app/Application.h"

#include "core/BufferManager.h"
#include "core/Paths.h"
#include "core/SessionStore.h"
#include "core/Settings.h"
#include "theme/ChromeStyle.h"
#include "theme/Fonts.h"
#include "theme/ThemeManager.h"
#include "ui/MainWindow.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLocalSocket>
#include <QStyleFactory>

#include <unistd.h>

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv) {
    setApplicationName(QStringLiteral("loom"));
    setApplicationVersion(QStringLiteral("0.1.0"));
    setOrganizationName(QStringLiteral("loom"));
    setDesktopFileName(QStringLiteral("loom"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/loom.svg")));
    setAttribute(Qt::AA_DontUseNativeDialogs);
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        setStyle(new ChromeStyle(fusion));
    }
    QIcon::setThemeName(QStringLiteral("loom"));
    QIcon::setFallbackThemeName(QString());
    Fonts::loadEmbedded();

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("minimal retro markdown scratchpad"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"), QStringLiteral("markdown files to open"),
                                 QStringLiteral("[files...]"));
    parser.process(*this);
    pendingFiles_ = parser.positionalArguments();
}

bool Application::acquireSingleton(const QStringList& files) {
    const QString name = QStringLiteral("loom-%1").arg(::getuid());
    QLocalSocket socket;
    socket.connectToServer(name);
    if (socket.waitForConnected(150)) {
        QString payload = files.join(QLatin1Char('\n'));
        if (payload.isEmpty()) {
            payload = QStringLiteral("--new-tab");
        }
        socket.write(payload.toUtf8());
        socket.waitForBytesWritten(200);
        socket.flush();
        socket.disconnectFromServer();
        return false;
    }
    QLocalServer::removeServer(name);
    server_.listen(name);
    connect(&server_, &QLocalServer::newConnection, this, &Application::onSecondary);
    return true;
}

void Application::onSecondary() {
    QLocalSocket* socket = server_.nextPendingConnection();
    if (!socket) {
        return;
    }
    socket->waitForReadyRead(200);
    const QString msg = QString::fromUtf8(socket->readAll()).trimmed();
    socket->deleteLater();
    QStringList paths;
    if (msg != QLatin1String("--new-tab") && !msg.isEmpty()) {
        paths = msg.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    openInWindow(paths);
}

void Application::openInWindow(const QStringList& paths) {
    if (!window_) {
        return;
    }
    if (paths.isEmpty()) {
        window_->newScratchTab();
    } else {
        window_->openPaths(paths);
    }
    window_->raise();
    window_->activateWindow();
}

int Application::run() {
    if (!acquireSingleton(pendingFiles_)) {
        return 0;
    }

    Paths::ensureDirectories();
    Settings settings = Settings::load();
    buffers_ = new BufferManager(this);
    themes_ = new ThemeManager(this);
    themes_->apply(settings);

    Session session = SessionStore::load();
    if (!session.buffers.isEmpty()) {
        buffers_->restore(session);
    } else {
        buffers_->createScratch();
    }

    window_ = new MainWindow(buffers_, themes_, settings);
    window_->restorePrefs(session.zoom, session.zen, session.geometry);
    window_->show();

    if (!pendingFiles_.isEmpty()) {
        QStringList abs;
        for (const QString& p : pendingFiles_) {
            abs.push_back(QFileInfo(QDir::current(), p).absoluteFilePath());
        }
        window_->openPaths(abs);
    }
    return exec();
}
