#pragma once
#include <vector>
#include "SDK.h"

namespace GameReader {
    void Update(uintptr_t base);

    // Accessors
    uintptr_t GetLocalPlayer();
    const std::vector<GameObject*>& GetEnemies();
    const std::vector<GameObject*>& GetMinions();
    const std::vector<GameObject*>& GetMissiles();
    // Accessors
    float GetGameTime(); // Add this line
}