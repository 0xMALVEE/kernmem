#pragma once
#include <vector>
#include <cmath>
#include "SDK.h"
#include "Orbwalker.h"
#include "Utils.h"
#include "imgui.h"
#include "Settings.h" 
#include <string>
#include "Prediction.h"

namespace Overlays {

    inline void DrawAttackRange(GameObject* local, float range) {
        if (!local) return;
        if (!Settings::DrawAttackRange) return;

        ImDrawList* drawList = ImGui::GetBackgroundDrawList();
        Vector3 center = local->GetPos();
        const int segments = 60;
        const float step = 6.2831853f / segments;

        ImU32 color = ImGui::ColorConvertFloat4ToU32(ImVec4(
            Settings::RangeColor[0], Settings::RangeColor[1],
            Settings::RangeColor[2], Settings::RangeColor[3]
        ));

        std::vector<ImVec2> screenPoints;
        screenPoints.reserve(segments + 1);

        for (int i = 0; i <= segments; i++) {
            float theta = i * step;
            Vector3 worldPos = { center.x + range * cos(theta), center.y, center.z + range * sin(theta) };
            Vector2 screenPos;
            if (Utils::WorldToScreen(worldPos, screenPos)) screenPoints.push_back(ImVec2(screenPos.x, screenPos.y));
        }

        if (screenPoints.size() > 1) {
            for (size_t i = 0; i < screenPoints.size() - 1; i++) drawList->AddLine(screenPoints[i], screenPoints[i + 1], color, Settings::RangeThickness);
        }
    }

    inline void DrawMissiles() {
        if (!Settings::DrawMissiles) return;
        const auto& missiles = GameReader::GetMissiles();
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();

        for (auto* m : missiles) {
            std::string name = m->GetMissileSpellName();
            if (name.empty() || name.find("BasicAttack") != std::string::npos || name.find("sfx") != std::string::npos)
                continue;

            Vector3 current = m->GetMissileCurrentPos();
            Vector3 startPos = m->GetMissileStartPos();
            Vector3 endPos = m->GetMissileEndPos();

            // Ignore malformed objects
            if (current.x == 0 && current.z == 0) continue;
            if (endPos.x == 0 && endPos.z == 0) continue;

            // 1. CALCULATE DIRECTION DIRECTLY FROM MEMORY (No math caches needed anymore!)
            Vector3 dir = { endPos.x - startPos.x, 0.0f, endPos.z - startPos.z };
            float length = sqrtf(dir.x * dir.x + dir.z * dir.z);
            if (length == 0) continue;

            dir.x /= length;
            dir.z /= length;

            // 2. DEFINE SPELL GEOMETRY 
            float spellRadius = 80.0f; // Default bounding box width

            // 3. CALCULATE LIVE DANGER ZONE
            float distTraveled = current.distance(startPos);
            float distRemaining = current.distance(endPos);

            // Calculate perpendicular vector for width (-Z, X)
            Vector3 perp = { -dir.z, 0.0f, dir.x };

            // Calculate the 4 corners of the Evade Rectangle (From Current Head to Target End)
            Vector3 p1 = { current.x + perp.x * spellRadius, current.y, current.z + perp.z * spellRadius };
            Vector3 p2 = { current.x - perp.x * spellRadius, current.y, current.z - perp.z * spellRadius };
            Vector3 p3 = { endPos.x - perp.x * spellRadius, endPos.y, endPos.z - perp.z * spellRadius };
            Vector3 p4 = { endPos.x + perp.x * spellRadius, endPos.y, endPos.z + perp.z * spellRadius };

            // 4. DRAW IT
            Vector2 sp1, sp2, sp3, sp4, sCurr;
            if (Utils::WorldToScreen(p1, sp1) && Utils::WorldToScreen(p2, sp2) &&
                Utils::WorldToScreen(p3, sp3) && Utils::WorldToScreen(p4, sp4) &&
                Utils::WorldToScreen(current, sCurr))
            {
                ImVec2 points[4] = { ImVec2(sp1.x, sp1.y), ImVec2(sp2.x, sp2.y), ImVec2(sp3.x, sp3.y), ImVec2(sp4.x, sp4.y) };

                // Danger Rectangle
                drawList->AddConvexPolyFilled(points, 4, ImColor(255, 0, 0, 60));
                drawList->AddPolyline(points, 4, ImColor(255, 0, 0, 200), ImDrawFlags_Closed, 2.0f);

                // Missile Head
                drawList->AddCircleFilled(ImVec2(sCurr.x, sCurr.y), 8.0f, ImColor(255, 255, 255));
                drawList->AddCircle(ImVec2(sCurr.x, sCurr.y), 11.0f, ImColor(255, 0, 0), 12, 2.0f);

                // Name Tag
                drawList->AddText(ImVec2(sCurr.x - 30, sCurr.y - 45), ImColor(255, 255, 255), name.c_str());

                // --- LIVE DEBUG DATA ---
                char dbg[128];
                sprintf_s(dbg, "Traveled: %.0f | Rem: %.0f", distTraveled, distRemaining);
                drawList->AddText(ImVec2(sCurr.x - 30, sCurr.y - 30), ImColor(0, 255, 255), dbg);
            }
        }
    }

    inline void DrawPredictionPaths(const std::vector<GameObject*>& enemies) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();

        for (auto* enemy : enemies) {
            if (!IsValidPtr(enemy)) continue;

            // Only draw for alive targets
            float hp = SafeReadSDK<float>((uintptr_t)enemy + Offset::oHealth, 0);
            if (hp <= 0) continue;

            // Get the live calculated path
            std::vector<Vector3> path = Prediction::GetKinematicPath(enemy);

            if (path.size() >= 2) {
                Vector2 sStart, sEnd;
                if (Utils::WorldToScreen(path[0], sStart) && Utils::WorldToScreen(path[1], sEnd)) {

                    // Draw RED line representing their predicted movement
                    drawList->AddLine(ImVec2(sStart.x, sStart.y), ImVec2(sEnd.x, sEnd.y), ImColor(255, 0, 0, 200), 3.0f);

                    // Draw YELLOW circle at the 1.5-second projection endpoint
                    drawList->AddCircleFilled(ImVec2(sEnd.x, sEnd.y), 5.0f, ImColor(255, 255, 0, 255));

                    // Draw Text
                    drawList->AddText(ImVec2(sEnd.x + 10, sEnd.y), ImColor(255, 255, 0), "1.5s Predicted");
                }
            }
        }
    }
}