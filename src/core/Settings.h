#pragma once

#include <QString>

struct Settings {
    QString bodyFont = QStringLiteral("Departure Mono");
    QString chromeFont = QStringLiteral("Departure Mono");
    double bodyPointSize = 13.5;
    double lineHeight = 1.5;
    QString themeSource = QStringLiteral("omarchy");
    QString customThemePath;
    double scanlineIntensity = 0.04;
    bool blockCaret = true;
    bool zenByDefault = false;
    bool autosaveNamedFiles = true;
    bool crtWipe = true;
    bool keyclick = false;
    QString notesDirectory;
    // Last template used by the hidden pdf export (Ctrl+Shift+P). Intentionally
    // absent from the settings dialog.
    QString pdfTemplate = QStringLiteral("paper");
    // Radio. The mini-player toggle lives in the radio dialog, not here.
    bool radioMiniPlayer = true;
    double radioVolume = 0.8;
    QString radioLastStation;

    static Settings load();
    bool save(QString* error = nullptr) const;

    QString resolvedNotesDirectory() const;
};
