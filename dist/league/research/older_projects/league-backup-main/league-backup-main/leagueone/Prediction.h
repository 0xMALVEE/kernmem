#pragma once
#include "SDK.h"
#include "Vector.h"
#include <vector>

enum class SpellType { Line, Circular, Cone, Ring };
enum class HitChance { Impossible, Low, Medium, High, Immobile, Dashing };

struct PredictionInput {
    GameObject* Target = nullptr;
    float Delay = 0.0f;
    float Radius = 0.0f;
    float Speed = 0.0f;
    float Range = 0.0f;
    Vector3 From = { 0,0,0 };
    SpellType Type = SpellType::Line;
    bool CheckCollision = false;
};

struct PredictionOutput {
    Vector3 CastPosition = { 0,0,0 };
    HitChance HChance = HitChance::Impossible;
    bool IsValid = false;
};

namespace Prediction {
    // --- KINEMATIC ENGINE ---
    // Tracks enemy movements perfectly without reading AIManager
    void UpdateTrackers(const std::vector<GameObject*>& units);
    std::vector<Vector3> GetKinematicPath(GameObject* target);

    // Main API
    PredictionOutput GetPrediction(const PredictionInput& input);

    // Core Math
    std::vector<Vector3> CutPath(const std::vector<Vector3>& path, float distance);
    float GetPathLength(const std::vector<Vector3>& path);
    Vector3 CalculateTargetPosition(GameObject* target, float delay, float radius, float speed, const Vector3& from);

    // Collision
    bool CheckMinionCollision(const Vector3& from, const Vector3& targetPos, float spellWidth);
}