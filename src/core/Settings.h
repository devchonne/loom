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
    // Vault: an optional folder-scoped workspace. Off unless the user turns it
    // on in settings, and off is genuinely free — no index, no watcher, no
    // sidebar widget is ever built. vaultRoot is the active vault; the list of
    // known vaults lives in ~/.local/state/loom/vaults.json.
    bool vaultEnabled = false;
    QString vaultRoot;
    // The tree is the optional part of the optional feature. Ctrl+\ toggles it,
    // and when hidden there is no trace of it in the chrome at all.
    bool vaultSidebarVisible = false;
    int vaultSidebarWidth = 240;
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
    // The vault root to actually use, or empty when the feature is off. Every
    // vault code path hangs off this being non-empty.
    QString activeVaultRoot() const;
};
