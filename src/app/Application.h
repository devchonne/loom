#pragma once

#include <QApplication>
#include <QLocalServer>
#include <QStringList>

class BufferManager;
class MainWindow;
class ThemeManager;
struct Settings;

class Application : public QApplication {
    Q_OBJECT

public:
    Application(int& argc, char** argv);
    int run();
    void openInWindow(const QStringList& paths);

private:
    bool acquireSingleton(const QStringList& files);
    void onSecondary();

    QLocalServer server_;
    BufferManager* buffers_ = nullptr;
    ThemeManager* themes_ = nullptr;
    MainWindow* window_ = nullptr;
    QStringList pendingFiles_;
};
