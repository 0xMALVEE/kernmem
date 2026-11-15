#include "Prediction.h"
#include "GameReader.h"
#include "Offsets.h"
#include "Utils.h"
#include "Logger.h"
#include <map>

namespace Prediction {

    // --- KINEMATIC TRACKER ---
    struct UnitTracker {
        Vector3 LastPos;
        float LastTime;
        Vector3 Direction;
        float Speed;
    };

    static std::map<uintptr_t, UnitTracker> Trackers;

    void UpdateTrackers(const std::vector<GameObject*>& units) {
        float currentTime = Utils::GetGameTime();

        for (auto* unit : units) {
            if (!IsValidPtr(unit)) continue;

            uintptr_t addr = (uintptr_t)unit;
            Vector3 currentPos = unit->GetPos();

            if (Trackers.find(addr) == Trackers.end()) {
                Trackers[addr] = { currentPos, currentTime, {0,0,0}, 0.0f };
                continue;
            }

            auto& trk = Trackers[addr];
            float dt = currentTime - trk.LastTime;

            // Update every ~50ms to ensure we have a stable distance gap for a clean direction vector
            if (dt >= 0.05f) {
                float dist = currentPos.distance(trk.LastPos);

                // If they moved enough to establish a real direction
                if (dist > 5.0f && dist < 1000.0f) {

                    // >> USE OFFICIAL MOVESPEED FROM MEMORY <<
                    float officialMS = unit->GetMoveSpeed();
                    trk.Speed = (officialMS > 50.0f) ? officialMS : (dist / dt); // Fallback if MS is bugged

                    // Calculate normalized direction vector
                    Vector3 dir = { currentPos.x - trk.LastPos.x, 0.0f, currentPos.z - trk.LastPos.z };
                    float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                    if (len > 0) {
                        trk.Direction = { dir.x / len, 0.0f, dir.z / len };
                    }
                }
                else if (dist <= 2.0f) {
                    // Target stopped moving
                    trk.Speed = 0.0f;
                }

                trk.LastPos = currentPos;
                trk.LastTime = currentTime;
            }
        }
    }

    std::vector<Vector3> GetKinematicPath(GameObject* target) {
        std::vector<Vector3> path;
        Vector3 currentPos = target->GetPos();
        path.push_back(currentPos);

        uintptr_t addr = (uintptr_t)target;
        if (Trackers.find(addr) != Trackers.end()) {
            auto& trk = Trackers[addr];

            // If they are actively moving, project a line 1.5 seconds ahead
            if (trk.Speed > 50.0f) {
                Vector3 futurePos = {
                    currentPos.x + (trk.Direction.x * trk.Speed * 1.5f),
                    currentPos.y,
                    currentPos.z + (trk.Direction.z * trk.Speed * 1.5f)
                };
                path.push_back(futurePos);
            }
        }
        return path;
    }

    // -------------------------

    float GetPathLength(const std::vector<Vector3>& path) {
        float result = 0.0f;
        for (size_t i = 0; i < path.size() - 1; i++) {
            result += path[i].distance(path[i + 1]);
        }
        return result;
    }

    std::vector<Vector3> CutPath(const std::vector<Vector3>& path, float distance) {
        std::vector<Vector3> result;
        if (path.empty()) return result;
        if (distance <= 0) return path;

        float remaining = distance;
        for (size_t i = 0; i < path.size() - 1; i++) {
            float dist = path[i].distance(path[i + 1]);
            if (dist >= remaining) {
                Vector3 dir = { path[i + 1].x - path[i].x, 0.0f, path[i + 1].z - path[i].z };
                float len = sqrtf(dir.x * dir.x + dir.z * dir.z);
                if (len > 0) {
                    dir.x /= len; dir.z /= len;
                }

                Vector3 newStart = { path[i].x + (dir.x * remaining), path[i].y, path[i].z + (dir.z * remaining) };
                result.push_back(newStart);

                for (size_t j = i + 1; j < path.size(); j++) {
                    result.push_back(path[j]);
                }
                remaining = 0;
                break;
            }
            else {
                remaining -= dist;
            }
        }

        if (result.empty() && !path.empty()) {
            result.push_back(path.back());
        }

        return result;
    }

    Vector3 CalculateTargetPosition(GameObject* target, float delay, float radius, float speed, const Vector3& from) {
        Vector3 currentPos = target->GetPos();
        uintptr_t addr = (uintptr_t)target;

        // If we have kinematic data and they are actively moving
        if (Trackers.find(addr) != Trackers.end() && Trackers[addr].Speed > 50.0f) {
            auto& trk = Trackers[addr];

            // Add your ping to the delay (Roughly 0.035s average server response time)
            // You can increase this if it still shoots barely behind!
            float totalDelay = delay + 0.035f;

            float t = totalDelay;
            Vector3 predictedPos = currentPos;

            // ITERATIVE INTERCEPTION ENGINE
            // Converges on the exact collision point between missile and target
            for (int i = 0; i < 4; i++) {
                // 1. Where will the target be in 't' seconds?
                predictedPos = {
                    currentPos.x + (trk.Direction.x * trk.Speed * t),
                    currentPos.y,
                    currentPos.z + (trk.Direction.z * trk.Speed * t)
                };

                // 2. How long does the missile take to reach that new position?
                float flightTime = 0.0f;
                if (speed > 0.0f && speed < 99999.0f) {
                    flightTime = from.distance(predictedPos) / speed;
                }

                // 3. Update our time estimate (Cast Animation + Missile Flight Time)
                t = totalDelay + flightTime;
            }

            return predictedPos;
        }

        // If they are standing still
        return currentPos;
    }

    // ... [Keep existing CheckMinionCollision and GetPrediction functions unchanged below]
    bool CheckMinionCollision(const Vector3& from, const Vector3& targetPos, float spellWidth) {
        float collisionRadius = 65.0f + spellWidth; // Standard Minion Hitbox + Spell Width
        auto local = (GameObject*)GameReader::GetLocalPlayer();
        int myTeam = local->GetTeam();

        for (auto* minion : GameReader::GetMinions()) {
            if (!IsValidPtr(minion)) continue;

            float hp = SafeReadSDK<float>((uintptr_t)minion + Offset::oHealth, 0);
            if (hp <= 0.0f) continue;

            unsigned char isTargetable = SafeReadSDK<unsigned char>((uintptr_t)minion + Offset::oTargetable, 0);
            if (isTargetable != 1) continue;

            int minionTeam = minion->GetTeam();
            if (minionTeam == myTeam || minionTeam == 0) continue;

            Vector3 mPos = minion->GetPos();
            float distToLine = mPos.distanceToSegment2D(from, targetPos);

            if (distToLine <= collisionRadius) {
                return true;
            }
        }
        return false;
    }

    PredictionOutput GetPrediction(const PredictionInput& input) {
        PredictionOutput output;
        if (!input.Target) return output;

        // 1. Calculate base interception point
        Vector3 castPos = CalculateTargetPosition(input.Target, input.Delay, input.Radius, input.Speed, input.From);

        // 2. Validate Range
        if (input.From.distance(castPos) > input.Range) {
            output.HChance = HitChance::Impossible;
            return output;
        }

        output.CastPosition = castPos;
        output.HChance = HitChance::High; // Baseline High if in range. In the future we will check 'IsDashing' etc.

        // 3. Evaluate Collision
        if (input.CheckCollision && input.Type == SpellType::Line) {
            if (CheckMinionCollision(input.From, castPos, input.Radius)) {
                output.HChance = HitChance::Low; // Blocked
            }
        }

        output.IsValid = (output.HChance != HitChance::Impossible && output.HChance != HitChance::Low);
        return output;
    }
}