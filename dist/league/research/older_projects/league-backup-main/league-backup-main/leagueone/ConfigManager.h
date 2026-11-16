#pragma once

namespace ConfigManager {
    // Loads settings from Settings.toml, overwriting defaults.
    // Called once at startup.
    void LoadSettings();

    // Saves the current settings to Settings.toml.
    // Called whenever a setting is changed in the UI.
    void SaveSettings();
}