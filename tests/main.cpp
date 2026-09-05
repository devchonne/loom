#include <QApplication>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
    // Editor-level tests instantiate real widgets, so the suite needs a
    // QApplication with a usable platform plugin. Offscreen keeps it headless.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    // The gtk3 platform theme plugin aborts when it cannot open a display, so a
    // desktop session's QT_QPA_PLATFORMTHEME would break a headless ctest run.
    qputenv("QT_QPA_PLATFORMTHEME", "");
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("loom"));
    QCoreApplication::setOrganizationName(QStringLiteral("loom"));
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
