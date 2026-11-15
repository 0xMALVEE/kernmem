#pragma once
#include "SDK.h"
#include <vector>

namespace TargetSelector {
    GameObject* GetBestHero(const std::vector<GameObject*>& heroes, GameObject* local, float range);
    GameObject* GetBestMinion(const std::vector<GameObject*>& minions, GameObject* local, float range);
}