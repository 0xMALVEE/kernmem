
#include "Ezreal.h"
#include "imgui.h"
#include "GameReader.h"
#include "TargetSelector.h"
#include "Input.h"
#include "Settings.h"
#include "Utils.h"
#include "Offsets.h"
#include "Logger.h"
#include "GameAPI.h" 
#include "Prediction.h" 

// --- DYNAMIC DAMAGE ENGINE ---
float GetEzrealQRawDamage(int level) {
    if (level < 1 || level > 5) return 0.0f;
    float base[] = { 20.0f, 45.0f, 70.0f, 95.0f, 120.0f };
    return base[level - 1] + (1.30f * GameAPI::CurrentAttackDamage) + (0.15f * GameAPI::CurrentAbilityPower);
}

float GetEzrealWRawDamage(int level) {
    if (level < 1 || level > 5) return 0.0f;
    float base[] = { 80.0f, 135.0f, 190.0f, 245.0f, 300.0f };
    float apRatio[] = { 0.70f, 0.75f, 0.80f, 0.85f, 0.90f };
    float bonusAD = fmax(0.0f, GameAPI::CurrentAttackDamage - 80.0f); // Approx Base AD
    return base[level - 1] + (0.60f * bonusAD) + (apRatio[level - 1] * GameAPI::CurrentAbilityPower);
}

float GetEzrealERawDamage(int level) {
    if (level < 1 || level > 5) return 0.0f;
    float base[] = { 80.0f, 130.0f, 180.0f, 230.0f, 280.0f };
    float bonusAD = fmax(0.0f, GameAPI::CurrentAttackDamage - 80.0f);
    return base[level - 1] + (0.50f * bonusAD) + (0.75f * GameAPI::CurrentAbilityPower);
}

float CalculateEffectivePhysicalDamage(float rawDamage, float targetArmor) {
    float effArmor = (targetArmor * GameAPI::CurrentArmorPenPercent) - GameAPI::CurrentLethality;
    if (effArmor < 0.0f) effArmor = 0.0f;
    return (effArmor >= 0.0f) ? (rawDamage * (100.0f / (100.0f + effArmor))) : (rawDamage * (2.0f - (100.0f / (100.0f - effArmor))));
}

float CalculateEffectiveMagicDamage(float rawDamage, float targetMR) {
    float effMR = (targetMR * GameAPI::CurrentMagicPenPercent) - GameAPI::CurrentMagicPenFlat;
    if (effMR < 0.0f) effMR = 0.0f;
    return (effMR >= 0.0f) ? (rawDamage * (100.0f / (100.0f + effMR))) : (rawDamage * (2.0f - (100.0f / (100.0f - effMR))));
}

// --- FULL COMBO CALCULATOR ---
float GetFullComboDamage(GameObject* local, GameObject* target, float realTime) {
    if (!local || !target) return 0.0f;

    float totalDamage = 0.0f;
    float armor = target->GetArmor();
    float mr = target->GetMagicResist();

    SpellSlot qSlot = local->GetSpellSlot(0);
    SpellSlot wSlot = local->GetSpellSlot(1);
    SpellSlot eSlot = local->GetSpellSlot(2);

    if (qSlot.IsReady(realTime)) totalDamage += CalculateEffectivePhysicalDamage(GetEzrealQRawDamage(qSlot.GetLevel()), armor);
    if (wSlot.IsReady(realTime)) totalDamage += CalculateEffectiveMagicDamage(GetEzrealWRawDamage(wSlot.GetLevel()), mr);
    if (eSlot.IsReady(realTime)) totalDamage += CalculateEffectiveMagicDamage(GetEzrealERawDamage(eSlot.GetLevel()), mr);

    return totalDamage;
}

void Ezreal::Initialize() {
    Logger::Add("[EZREAL] Core loaded.");
}

void Ezreal::OnTick() {
    bool isCombo = (GetAsyncKeyState(Settings::ComboKey) & 0x8000);
    if (!isCombo) return;
    if (!Input::IsReady()) return;

    GameObject* local = (GameObject*)GameReader::GetLocalPlayer();
    float realTime = GameReader::GetGameTime();

    SpellSlot qSlot = local->GetSpellSlot(0);
    SpellSlot wSlot = local->GetSpellSlot(1);
    SpellSlot eSlot = local->GetSpellSlot(2);

    GameObject* target = TargetSelector::GetBestHero(GameReader::GetEnemies(), local, 1150.0f);
    if (!target) return;

    PredictionInput qInput;
    qInput.Target = target;
    qInput.From = local->GetPos();
    qInput.Delay = 0.25f;
    qInput.Speed = 2000.0f;
    qInput.Radius = 60.0f;
    qInput.Range = 1150.0f;
    qInput.Type = SpellType::Line;
    qInput.CheckCollision = true;

    PredictionInput wInput;
    wInput.Target = target;
    wInput.From = local->GetPos();
    wInput.Delay = 0.25f;
    wInput.Speed = 1700.0f;
    wInput.Radius = 60.0f;
    wInput.Range = 1150.0f;
    wInput.Type = SpellType::Line;
    wInput.CheckCollision = false;

    PredictionOutput qPred = Prediction::GetPrediction(qInput);
    PredictionOutput wPred = Prediction::GetPrediction(wInput);

    float targetHp = target->GetHealth();
    float comboDmg = GetFullComboDamage(local, target, realTime);
    bool isLethal = (targetHp > 0.0f && targetHp <= (comboDmg - 5.0f));

    // --- REPOSITION E LOGIC (Aggressive & Safe) ---
    if (useEInCombo && eSlot.IsReady(realTime) && (realTime - lastECastTime > 0.5f)) {

        Vector3 bestEPos = { 0, 0, 0 };
        float bestMouseDist = 999999.0f;
        float minSafeDist = 450.0f; // Minimum allowed distance from target to avoid jumping into their face

        // Get player's current mouse position to guide the jump
        POINT mousePos; GetCursorPos(&mousePos);
        Vector2 mouseVec = { (float)mousePos.x, (float)mousePos.y };

        // Test 24 different points around Ezreal at max E range (475)
        for (int i = 0; i < 360; i += 15) {
            float rad = i * (3.14159f / 180.f);
            Vector3 testPos = {
                local->GetPos().x + 475.0f * cos(rad),
                local->GetPos().y,
                local->GetPos().z + 475.0f * sin(rad)
            };

            float distToTarget = testPos.distance(target->GetPos());

            // 1. Maintain safe distance from target (Don't jump melee, but stay in Q range)
            if (distToTarget >= minSafeDist && distToTarget <= 1100.0f) {

                // 2. Make sure Q will NOT hit a minion from this new angle
                if (!Prediction::CheckMinionCollision(testPos, target->GetPos(), 60.0f)) {

                    // 3. Project testPos to screen
                    Vector2 screenPos;
                    if (Utils::WorldToScreen(testPos, screenPos)) {

                        // Pick the safe/clear spot that is closest to where the player is aiming their mouse!
                        float dx = screenPos.x - mouseVec.x;
                        float dy = screenPos.y - mouseVec.y;
                        float distToMouse = sqrtf(dx * dx + dy * dy);

                        if (distToMouse < bestMouseDist) {
                            bestMouseDist = distToMouse;
                            bestEPos = testPos;
                        }
                    }
                }
            }
        }

        // If we found a perfectly safe, unblocked spot that aligns with our mouse, cast E!
        if (bestEPos.x != 0) {
            Input::FlickKey('E', bestEPos);
            lastECastTime = realTime;
            return; // Return so we don't cast Q during the E animation
        }
    }

    // --- COMBO LOGIC ---
    if (isLethal && useQInCombo && qSlot.IsReady(realTime) && (realTime - lastQCastTime > 0.2f)) {
        if (qPred.IsValid) {
            Input::FlickKey('Q', qPred.CastPosition);
            lastQCastTime = realTime;
            return;
        }
    }

    if (prioritizeW) {
        if (useWInCombo && wSlot.IsReady(realTime) && (realTime - lastWCastTime > 0.2f) && wPred.IsValid) {
            Input::FlickKey('W', wPred.CastPosition);
            lastWCastTime = realTime;
            return;
        }
        if (useQInCombo && qSlot.IsReady(realTime) && (realTime - lastQCastTime > 0.2f) && qPred.IsValid) {
            Input::FlickKey('Q', qPred.CastPosition);
            lastQCastTime = realTime;
            return;
        }
    }
    else {
        if (useQInCombo && qSlot.IsReady(realTime) && (realTime - lastQCastTime > 0.2f) && qPred.IsValid) {
            Input::FlickKey('Q', qPred.CastPosition);
            lastQCastTime = realTime;
            return;
        }
        if (useWInCombo && wSlot.IsReady(realTime) && (realTime - lastWCastTime > 0.2f) && wPred.IsValid) {
            Input::FlickKey('W', wPred.CastPosition);
            lastWCastTime = realTime;
            return;
        }
    }
}

// --- CUSTOM ESP DAMAGE BAR ---
void Ezreal::OnDraw() {
    if (!drawDamage) return;

    GameObject* local = (GameObject*)GameReader::GetLocalPlayer();
    if (!local) return;

    float realTime = GameReader::GetGameTime();
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    for (auto* enemy : GameReader::GetEnemies()) {
        if (!IsValidPtr(enemy)) continue;

        float hp = enemy->GetHealth();
        float maxHp = enemy->GetMaxHealth();
        if (hp <= 0.0f || maxHp <= 0.0f) continue;

        Vector2 sPos;
        if (!Utils::WorldToScreen(enemy->GetPos(), sPos)) continue;

        float comboDamage = GetFullComboDamage(local, enemy, realTime);
        if (comboDamage <= 0.0f) continue;

        float barWidth = 80.0f;
        float barHeight = 8.0f;

        float startX = sPos.x - (barWidth / 2.0f);
        float startY = sPos.y + 15.0f;

        float hpPercent = hp / maxHp;
        float dmgPercent = comboDamage / maxHp;
        if (dmgPercent > hpPercent) dmgPercent = hpPercent;

        float currentHpWidth = barWidth * hpPercent;
        float damageWidth = barWidth * dmgPercent;

        drawList->AddRectFilled(ImVec2(startX - 1, startY - 1), ImVec2(startX + barWidth + 1, startY + barHeight + 1), ImColor(0, 0, 0, 255));
        drawList->AddRectFilled(ImVec2(startX, startY), ImVec2(startX + currentHpWidth, startY + barHeight), ImColor(150, 0, 0, 255));

        float damageStartX = startX + currentHpWidth - damageWidth;
        drawList->AddRectFilled(ImVec2(damageStartX, startY), ImVec2(startX + currentHpWidth, startY + barHeight), ImColor(255, 140, 0, 255));

        if (comboDamage >= hp) {
            float time = Utils::GetGameTime() * 5.0f;
            int alpha = (int)((sin(time) + 1.0f) * 127.5f);
            drawList->AddText(ImVec2(startX + (barWidth / 2.0f) - 35.0f, startY + 12.0f), ImColor(255, 50, 50, alpha), "LETHAL!");
        }
        else {
            char dmgText[32];
            sprintf_s(dmgText, "-%.0f", comboDamage);
            drawList->AddText(ImVec2(startX + barWidth + 5.0f, startY - 3.0f), ImColor(255, 140, 0, 255), dmgText);
        }
    }
}

void Ezreal::OnMenu() {
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Ezreal Combo Settings");
    ImGui::Separator();

    ImGui::Checkbox("Use Q in Combo", &useQInCombo);
    ImGui::Checkbox("Use W in Combo", &useWInCombo);
    ImGui::Checkbox("Use E Aggressively in Combo", &useEInCombo);

    ImGui::Spacing();
    ImGui::Text("Logic Priority");
    if (ImGui::RadioButton("W -> Q (Burst)", prioritizeW == true)) prioritizeW = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Q -> W (Poke)", prioritizeW == false)) prioritizeW = false;

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0, 1, 1, 1), "Visuals");
    ImGui::Checkbox("Draw ESP Damage Bar", &drawDamage);
}