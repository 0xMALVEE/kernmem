#include "TargetSelector.h"
#include "Offsets.h"
#include "Utils.h"
#include "Logger.h"
#include <cmath>
#include "Settings.h"

namespace TargetSelector {

    // --- CHAMPION LOGIC ---
    bool IsValidHero(GameObject* unit, int myTeam, float range, GameObject* local) {
        if (!IsValidPtr(unit)) return false;
        if (unit->GetTeam() == myTeam) return false;

        float hp = SafeReadSDK<float>((uintptr_t)unit + Offset::oHealth, 0);
        if (hp <= 1.0f) return false;

        unsigned char isTargetable = SafeReadSDK<unsigned char>((uintptr_t)unit + Offset::oTargetable, 0);
        if (isTargetable != 1) return false;

        float dist = local->GetPos().distance(unit->GetPos());
        if (dist > (range + 100.0f)) return false;

        return true;
    }

    GameObject* GetBestHero(const std::vector<GameObject*>& heroes, GameObject* local, float range) {
        GameObject* best = nullptr;
        int myTeam = local->GetTeam();
        POINT mousePos; GetCursorPos(&mousePos);

        // Mode 0: Closest to Mouse
        if (Settings::TargetMode == 0) {
            float bestDist = 99999.0f;
            for (auto* unit : heroes) {
                if (!IsValidHero(unit, myTeam, range, local)) continue;
                Vector2 sPos;
                if (Utils::WorldToScreen(unit->GetPos(), sPos)) {
                    float dx = sPos.x - mousePos.x;
                    float dy = sPos.y - mousePos.y;
                    float mouseDist = sqrt(dx * dx + dy * dy);
                    if (mouseDist < 600.0f && mouseDist < bestDist) {
                        bestDist = mouseDist;
                        best = unit;
                    }
                }
            }
        }
        // Mode 1: Lowest HP
        else if (Settings::TargetMode == 1) {
            float lowestHp = 999999.0f;
            for (auto* unit : heroes) {
                if (!IsValidHero(unit, myTeam, range, local)) continue;
                float hp = SafeReadSDK<float>((uintptr_t)unit + Offset::oHealth, 0);
                if (hp < lowestHp) {
                    lowestHp = hp;
                    best = unit;
                }
            }
        }
        // Mode 2: Priority List
        else if (Settings::TargetMode == 2) {
            int bestPriority = 6;
            float bestHp = 999999.0f; // Used as a tie-breaker

            for (auto* unit : heroes) {
                if (!IsValidHero(unit, myTeam, range, local)) continue;

                std::string name = unit->GetChampionName();
                int prio = 5; // Default lowest priority
                if (Settings::PriorityMap.count(name)) {
                    prio = Settings::PriorityMap[name];
                }

                float hp = SafeReadSDK<float>((uintptr_t)unit + Offset::oHealth, 0);

                if (prio < bestPriority) {
                    bestPriority = prio;
                    bestHp = hp;
                    best = unit;
                }
                else if (prio == bestPriority) {
                    // Tie breaker: lower HP
                    if (hp < bestHp) {
                        bestHp = hp;
                        best = unit;
                    }
                }
            }
        }

        return best;
    }


    // --- MINION LOGIC ---
    GameObject* GetBestMinion(const std::vector<GameObject*>& minions, GameObject* local, float range) {
        GameObject* best = nullptr;
        float bestDistToPlayer = 99999.0f;

        int rawPTeam = local->GetTeam();
        int pTeamNorm = 0;
        if (rawPTeam == 100 || rawPTeam == 1) pTeamNorm = 1;
        if (rawPTeam == 200 || rawPTeam == 2) pTeamNorm = 2;

        for (auto* unit : minions) {
            float dist = local->GetPos().distance(unit->GetPos());
            if (dist > (range + 150.0f)) continue;

            float hp = SafeReadSDK<float>((uintptr_t)unit + Offset::oHealth, 0);
            if (hp <= 0.0f) continue;

            unsigned char isTargetable = SafeReadSDK<unsigned char>((uintptr_t)unit + Offset::oTargetable, 0);
            if (isTargetable != 1) continue;

            int mTeamNorm = 0;
            int rawMTeam = unit->GetMinionTeam();
            if (rawMTeam == 100) mTeamNorm = 1;
            if (rawMTeam == 200) mTeamNorm = 2;
            if (rawMTeam == 300) mTeamNorm = 3;

            if (pTeamNorm != 0 && pTeamNorm == mTeamNorm) continue;
            if (mTeamNorm == 0) continue;

            if (dist < bestDistToPlayer) {
                bestDistToPlayer = dist;
                best = unit;
            }
        }
        return best;
    }
}