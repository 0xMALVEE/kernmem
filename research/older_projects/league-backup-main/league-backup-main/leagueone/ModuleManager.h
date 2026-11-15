#pragma once
#include "IChampion.h"
#include <vector>

namespace ModuleManager {
    void RegisterModule(IChampion* championModule);
    void Initialize();

    void OnTick();
    void OnDraw();
    void OnMenu();

    extern IChampion* ActiveModule;
}