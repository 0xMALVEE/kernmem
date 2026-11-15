#pragma once
#include <Windows.h> // For VK codes
#include <map>
#include <string>

namespace Settings {
    // --- Global Toggles ---
    inline bool ShowMenu = false;

    // --- Orbwalker Toggles ---
    inline bool OrbwalkerEnabled = true;
    inline bool AttackUnit = true;
    inline bool MoveUnit = true;

    // --- Target Selector ---
    inline int TargetMode = 0; // 0: Closest to Mouse, 1: Lowest HP, 2: Priority
    inline std::map<std::string, int> PriorityMap;

    // --- New Feature: Auto TCO ---
    inline bool UseTCO = true; // Master switch for this feature
    inline bool IsTCOToggle = false; // New: Detected from config

    // --- Keys ---
    inline int ComboKey = VK_SPACE;
    inline int LaneClearKey = 'V'; // 0x56
    inline int AttackChampionsOnlyKey = 'G'; // Default to G (User needs to bind this in menu)

    // --- Orbwalker Tuning ---
    inline float WindupPercent = 0.225f;
    inline float BaseBuffer = 0.040f;
    inline float ClickHoldTime = 0.025f;
    inline float MoveDelay = 0.090f;

    // --- NEW: Flick Delays (Milliseconds) ---
    inline float FlickPreDelay = 1.5f;   // Time to let OS teleport mouse
    inline float FlickPressDelay = 5.0f; // Time to hold the key/click down
    inline float FlickPostDelay = 1.5f;  // Time to let OS register the key up

    // --- Overlay Settings ---
    inline bool DrawAttackRange = true;
    inline float RangeThickness = 2.0f;
    inline float RangeColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f }; // White

    inline bool ZoomHackEnabled = false;
    inline float MaxZoomValue = 2250.0f; // Default

    inline bool DrawMissiles = true;
}

// Holds live, per-frame data for UI display
namespace Debug {
    inline int CurrentPing = 0;
    inline float CurrentBuffer = 0.0f;
}