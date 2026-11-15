#pragma once
#include "IChampion.h"

class Ezreal : public IChampion {
public:
    std::string GetName() const override { return "Ezreal"; }

    void Initialize() override;
    void OnTick() override;
    void OnDraw() override;
    void OnMenu() override;

private:
    bool useQInCombo = true;
    bool useWInCombo = true;
    bool useEInCombo = true;
    bool prioritizeW = true; // NEW: Combo Logic Toggle
    bool drawDamage = true; // NEW: Toggle for Damage Overlay

    // ADD THESE: Throttles to prevent spamming while waiting for game memory to update
    float lastQCastTime = 0.0f;
    float lastWCastTime = 0.0f;
    float lastECastTime = 0.0f;
};