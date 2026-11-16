
#include "Orbwalker.h"
#include "TargetSelector.h"
#include "Utils.h"
#include "Settings.h"
#include "Logger.h"
#include "GameAPI.h"
#include "Input.h" 
#include <iostream>

float lastAttackTime = 0.0f;
float lastMoveTime = 0.0f;
bool isTCOActive = false;

void Orbwalker::Initialize() {
    GameAPI::Start();
}

void Orbwalker::Update(GameObject* local, const std::vector<GameObject*>& heroes, const std::vector<GameObject*>& minions) {
    if (!IsValidPtr(local)) return;

    if (Settings::UseTCO && Settings::AttackChampionsOnlyKey != 0) {
        bool isCombo = (GetAsyncKeyState(Settings::ComboKey) & 0x8000);
        if (Settings::IsTCOToggle) {
            if (isCombo && !isTCOActive) { Input::TapKey(Settings::AttackChampionsOnlyKey); isTCOActive = true; }
            else if (!isCombo && isTCOActive) { Input::TapKey(Settings::AttackChampionsOnlyKey); isTCOActive = false; }
        }
        else {
            if (isCombo && !isTCOActive) { Input::SendKey(Settings::AttackChampionsOnlyKey, true); isTCOActive = true; }
            else if (!isCombo && isTCOActive) { Input::SendKey(Settings::AttackChampionsOnlyKey, false); isTCOActive = false; }
        }
    }

    bool isCombo = (GetAsyncKeyState(Settings::ComboKey) & 0x8000);
    bool isLaneClear = (GetAsyncKeyState(Settings::LaneClearKey) & 0x8000);

    Debug::CurrentPing = GameAPI::GetPing();
    float pingBuffer = (Debug::CurrentPing > 0) ? (static_cast<float>(Debug::CurrentPing) / 2000.0f) : 0.0f;
    Debug::CurrentBuffer = Settings::BaseBuffer + pingBuffer;

    if (!isCombo && !isLaneClear) return;
    if (!Settings::OrbwalkerEnabled) return;

    // Do not issue orders if the Micro-Thread is actively jumping the mouse
    if (!Input::IsReady()) return;

    float now = Utils::GetGameTime();
    float as = GameAPI::CurrentAttackSpeed;
    float range = GameAPI::CurrentRange;
    if (as < 0.2f) as = 0.65f;
    if (range < 100) range = 550;

    float delay = 1.0f / as;
    float windup = (delay * Settings::WindupPercent) + Debug::CurrentBuffer;
    if (windup < 0.10f) windup = 0.10f;

    GameObject* target = nullptr;
    if (isCombo) target = TargetSelector::GetBestHero(heroes, local, range);
    else if (isLaneClear) target = TargetSelector::GetBestMinion(minions, local, range);

    // Attack Logic (Uses High-Precision Right Click Flick)
    if (target && Settings::AttackUnit) {
        if (now >= lastAttackTime + delay) {
            Vector2 s;
            if (Utils::WorldToScreen(target->GetPos(), s)) {
                Input::FlickRightClick(s);
                lastAttackTime = now;
                lastMoveTime = now + windup;
                return;
            }
        }
    }

    // Movement Logic
    if (Settings::MoveUnit && now >= lastMoveTime) {
        static float nextMove = 0;
        if (now >= nextMove) {
            POINT currentPos; GetCursorPos(&currentPos);
            // This won't flick if the mouse is already here (Optimization kicks in)
            Input::FlickRightClick(Vector2((float)currentPos.x, (float)currentPos.y));
            nextMove = now + Settings::MoveDelay;
        }
    }
}