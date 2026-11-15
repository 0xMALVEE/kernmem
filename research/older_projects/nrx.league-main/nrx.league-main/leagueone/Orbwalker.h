#pragma once
#include "SDK.h"
#include <vector>

class Orbwalker {
public:
    static void Initialize();
    static void Update(GameObject* local, const std::vector<GameObject*>& heroes, const std::vector<GameObject*>& minions);
};