#include "GameReader.h"
#include "Offsets.h"
#include "Logger.h"
#include "Settings.h"
#include <algorithm>
#include <string>

namespace GameReader {
    uintptr_t localAddr = 0;
    std::vector<GameObject*> enemies;
    std::vector<GameObject*> minions;
    std::vector<GameObject*> missiles;

    uintptr_t GetLocalPlayer() { return localAddr; }
    const std::vector<GameObject*>& GetEnemies() { return enemies; }
    const std::vector<GameObject*>& GetMinions() { return minions; }
    const std::vector<GameObject*>& GetMissiles() { return missiles; }

    void Update(uintptr_t base) {
        enemies.clear();
        minions.clear();
        missiles.clear();
        localAddr = 0;

        if (base < 0x10000) return;
        localAddr = SafeReadSDK<uintptr_t>(base + Offset::oLocalPlayer, 0);

        // 1. HEROES
        uintptr_t heroMgr = SafeReadSDK<uintptr_t>(base + Offset::oHeroList, 0);
        if (heroMgr) {
            uintptr_t arr = SafeReadSDK<uintptr_t>(heroMgr + 0x08, 0);
            int count = SafeReadSDK<int>(heroMgr + 0x10, 0);
            if (count > 0 && count < 60 && arr) {
                for (int i = 0; i < count; i++) {
                    uintptr_t obj = SafeReadSDK<uintptr_t>(arr + (i * 8), 0);
                    if (obj && obj != localAddr) enemies.push_back((GameObject*)obj);
                }
            }
        }

        // 2. MINIONS
        uintptr_t minionMgr = SafeReadSDK<uintptr_t>(base + Offset::oMinionList, 0);
        if (minionMgr) {
            int count = SafeReadSDK<int>(minionMgr + Offset::LaneMinionCount, 0);
            uintptr_t arr = SafeReadSDK<uintptr_t>(minionMgr + Offset::LaneMinionArray, 0);

            if (count > 0 && count < 1000 && arr) {
                for (int i = 0; i < count; i++) {
                    uintptr_t m = SafeReadSDK<uintptr_t>(arr + (i * 8), 0);
                    if (m) {
                        float hp = SafeReadSDK<float>(m + Offset::oHealth, 0);
                        if (hp > 0.0f) minions.push_back((GameObject*)m);
                    }
                }
            }
        }

        // 3. MISSILES
        uintptr_t missileMgr = SafeReadSDK<uintptr_t>(base + Offset::oMissileList, 0);
        if (missileMgr > 0x10000) {
            uintptr_t arr = SafeReadSDK<uintptr_t>(missileMgr + 0x08, 0);
            int count = SafeReadSDK<int>(missileMgr + 0x10, 0);

            if (count > 0 && count < 1000 && IsValidPtr((void*)arr)) {
                for (int i = 0; i < count; i++) {
                    uintptr_t m = SafeReadSDK<uintptr_t>(arr + (i * 8), 0);
                    if (IsValidPtr((void*)m)) {
                        missiles.push_back((GameObject*)m);
                    }
                }
            }
        }
    }
    
    // Put this anywhere inside the namespace
    float GetGameTime() {
        uintptr_t base = (uintptr_t)GetModuleHandle(NULL);
        return SafeReadSDK<float>(base + Offset::oGametime, 0.0f);
    }
}