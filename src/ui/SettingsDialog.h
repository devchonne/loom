#pragma once

#include "core/Settings.h"

#include <QDialog>

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const Settings& settings, QWidget* parent = nullptr);
    Settings result() const { return result_; }

private:
    Settings result_;
};
