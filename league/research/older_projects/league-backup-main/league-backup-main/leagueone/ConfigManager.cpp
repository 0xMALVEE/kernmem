#include "ConfigManager.h"
#include "Settings.h"
#include "Logger.h"
#include "toml.hpp" // The library we just added
#include <fstream>
#include <iostream>

namespace ConfigManager {

    const std::string configFilename = "Settings.toml";

    void LoadSettings() {
        try {
            toml::table tbl = toml::parse_file(configFilename);
            Logger::Add("[CFG] Loading %s...", configFilename.c_str());

            // --- Orbwalker ---
            Settings::OrbwalkerEnabled = tbl["Orbwalker"]["Enabled"].value_or(Settings::OrbwalkerEnabled);
            Settings::AttackUnit = tbl["Orbwalker"]["AttackUnits"].value_or(Settings::AttackUnit);
            Settings::MoveUnit = tbl["Orbwalker"]["MoveToMouse"].value_or(Settings::MoveUnit);
            Settings::WindupPercent = tbl["Orbwalker"]["WindupPercent"].value_or(Settings::WindupPercent);
            Settings::BaseBuffer = tbl["Orbwalker"]["BufferSeconds"].value_or(Settings::BaseBuffer);
            Settings::MoveDelay = tbl["Orbwalker"]["MoveDelay"].value_or(Settings::MoveDelay);

            // --- Flick Tuning ---
            Settings::FlickPreDelay = tbl["Flick"]["PreDelay"].value_or(Settings::FlickPreDelay);
            Settings::FlickPressDelay = tbl["Flick"]["PressDelay"].value_or(Settings::FlickPressDelay);
            Settings::FlickPostDelay = tbl["Flick"]["PostDelay"].value_or(Settings::FlickPostDelay);

            // --- Visuals ---
            Settings::DrawAttackRange = tbl["Visuals"]["DrawAttackRange"].value_or(Settings::DrawAttackRange);
            Settings::RangeThickness = tbl["Visuals"]["RangeThickness"].value_or(Settings::RangeThickness);

            Settings::ZoomHackEnabled = tbl["Visuals"]["ZoomHackEnabled"].value_or(Settings::ZoomHackEnabled);
            Settings::MaxZoomValue = tbl["Visuals"]["MaxZoomValue"].value_or(Settings::MaxZoomValue);

            if (auto* arr = tbl["Visuals"]["RangeColor"].as_array()) {
                if (arr->size() == 4) {
                    for (int i = 0; i < 4; ++i) {
                        Settings::RangeColor[i] = arr->get(i)->value_or(Settings::RangeColor[i]);
                    }
                }
            }

            // --- Targeting ---
            Settings::UseTCO = tbl["Targeting"]["UseTCO"].value_or(Settings::UseTCO);
            Settings::TargetMode = tbl["Targeting"]["TargetMode"].value_or(Settings::TargetMode);

            if (auto* prioTable = tbl["Targeting"]["Priorities"].as_table()) {
                for (auto& [key, val] : *prioTable) {
                    if (val.is_integer()) {
                        Settings::PriorityMap[std::string(key)] = val.as_integer()->get();
                    }
                }
            }

            // --- Keybinds ---
            // Note: TCO key is auto-detected, so we don't save/load it.
            Settings::ComboKey = tbl["Keybinds"]["Combo"].value_or(Settings::ComboKey);
            Settings::LaneClearKey = tbl["Keybinds"]["LaneClear"].value_or(Settings::LaneClearKey);

        }
        catch (const toml::parse_error& err) {
            // If file doesn't exist, this is normal on first run.
            // If it exists and is broken, log the error.
            std::ifstream f(configFilename);
            if (f.good()) {
                Logger::Add("[CFG] Error parsing %s: %s", configFilename.c_str(), err.description().data());
            }
            else {
                Logger::Add("[CFG] %s not found. Using default settings.", configFilename.c_str());
            }
        }
    }

    void SaveSettings() {
        toml::table prioTable;
        for (const auto& [name, prio] : Settings::PriorityMap) {
            prioTable.insert(name, prio);
        }

        toml::table tbl = toml::table{
            { "Orbwalker", toml::table{
                { "Enabled", Settings::OrbwalkerEnabled },
                { "AttackUnits", Settings::AttackUnit },
                { "MoveToMouse", Settings::MoveUnit },
                { "WindupPercent", Settings::WindupPercent },
                { "BufferSeconds", Settings::BaseBuffer },
                { "MoveDelay", Settings::MoveDelay }
            }},
            { "Flick", toml::table{
                { "PreDelay", Settings::FlickPreDelay },
                { "PressDelay", Settings::FlickPressDelay },
                { "PostDelay", Settings::FlickPostDelay }
            }},
            { "Visuals", toml::table{
                { "ZoomHackEnabled", Settings::ZoomHackEnabled },
                { "MaxZoomValue", Settings::MaxZoomValue },
                { "DrawAttackRange", Settings::DrawAttackRange },
                { "RangeThickness", Settings::RangeThickness },
                { "RangeColor", toml::array{
                    Settings::RangeColor[0], Settings::RangeColor[1], Settings::RangeColor[2], Settings::RangeColor[3]
                }}
            }},
            { "Targeting", toml::table{
                { "UseTCO", Settings::UseTCO },
                { "TargetMode", Settings::TargetMode },
                { "Priorities", prioTable }
            }},
            { "Keybinds", toml::table{
                { "Combo", Settings::ComboKey },
                { "LaneClear", Settings::LaneClearKey }
            }}
        };

        std::ofstream file(configFilename);
        file << "# LeagueOne Settings | Automatically generated\n";
        file << tbl;
    }
}