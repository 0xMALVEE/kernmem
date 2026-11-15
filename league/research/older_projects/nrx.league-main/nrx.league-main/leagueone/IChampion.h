#pragma once
#include <string>

class IChampion {
public:
    virtual ~IChampion() = default;

    // The exact internal name of the champion (e.g., "Ezreal", "MissFortune")
    virtual std::string GetName() const = 0;

    // Lifecycle events
    virtual void Initialize() {}
    virtual void OnTick() {}
    virtual void OnDraw() {}
    virtual void OnMenu() {}
};