#include "ui/ThemedDialogs.h"

#include "theme/ChromeStyle.h"

#include <QFileDialog>

namespace {

QString execFileDialog(QFileDialog& dialog) {
    dialog.setOption(QFileDialog::DontUseNativeDialog);
    dialog.setIconProvider(chromeIconProvider());
    if (dialog.exec() == QDialog::Accepted) {
        return dialog.selectedFiles().value(0);
    }
    return {};
}

} // namespace

namespace ThemedDialogs {

QString getOpenFileName(QWidget* parent, const QString& caption, const QString& dir,
                        const QString& filter) {
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    return execFileDialog(dialog);
}

QString getSaveFileName(QWidget* parent, const QString& caption, const QString& dir,
                        const QString& filter) {
    QFileDialog dialog(parent, caption, dir, filter);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    return execFileDialog(dialog);
}

QString getExistingDirectory(QWidget* parent, const QString& caption, const QString& dir) {
    QFileDialog dialog(parent, caption, dir);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    return execFileDialog(dialog);
}

} // namespace ThemedDialogs
