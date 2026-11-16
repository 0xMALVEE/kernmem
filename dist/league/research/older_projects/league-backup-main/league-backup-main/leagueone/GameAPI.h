#pragma once
#include <atomic>

namespace GameAPI {
    // Shared Data
    extern std::atomic<float> CurrentAttackSpeed;
    extern std::atomic<float> CurrentRange;
    extern std::atomic<float> CurrentAttackDamage;
    extern std::atomic<float> CurrentAbilityPower;
    extern std::atomic<float> CurrentArmor;
    extern std::atomic<float> CurrentMR;

    // Physical Pen
    extern std::atomic<float> CurrentArmorPenPercent;
    extern std::atomic<float> CurrentLethality;

    // Magic Pen
    extern std::atomic<float> CurrentMagicPenPercent;
    extern std::atomic<float> CurrentMagicPenFlat;

    extern bool Running;

    // Functions
    void Start();   // Starts the HTTP thread
    int GetPing();  // Calls internal GetPing
}