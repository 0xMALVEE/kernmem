#pragma once
#include <cmath> // Fix for sqrtf

struct Vector2 {
    float x, y;
    Vector2() : x(0.f), y(0.f) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0.f), y(0.f), z(0.f) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    float distance(const Vector3& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return sqrtf(dx*dx + dy*dy + dz*dz);
    }

    // Math for Skillshot Collision
    float distanceToSegment2D(const Vector3& segmentStart, const Vector3& segmentEnd) const {
        float dx = segmentEnd.x - segmentStart.x;
        float dz = segmentEnd.z - segmentStart.z;
        float lengthSquared = dx * dx + dz * dz;

        if (lengthSquared == 0.0f) {
            float pdx = x - segmentStart.x;
            float pdz = z - segmentStart.z;
            return sqrtf(pdx * pdx + pdz * pdz);
        }

        float t = ((x - segmentStart.x) * dx + (z - segmentStart.z) * dz) / lengthSquared;
        t = fmaxf(0.0f, fminf(1.0f, t)); // Clamp to segment

        float projX = segmentStart.x + t * dx;
        float projZ = segmentStart.z + t * dz;

        float distDx = x - projX;
        float distDz = z - projZ;
        return sqrtf(distDx * distDx + distDz * distDz);
    }
};


