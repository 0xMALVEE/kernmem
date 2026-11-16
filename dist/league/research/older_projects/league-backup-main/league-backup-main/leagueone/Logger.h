#pragma once
#include "imgui.h"
#include <vector>
#include <string>
#include <cstdarg>

namespace Logger {
    inline std::vector<std::string> Logs;
    inline bool AutoScroll = true;

    inline void Add(const char* fmt, ...) {
        char buffer[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);

        Logs.push_back(std::string(buffer));
        if (Logs.size() > 1000) Logs.erase(Logs.begin());
    }

    inline void Draw(bool* p_open) {
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Debug Console", p_open)) {
            ImGui::End();
            return;
        }

        if (ImGui::Button("Clear")) Logs.clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &AutoScroll);
        ImGui::Separator();

        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& log : Logs) {
            ImGui::TextUnformatted(log.c_str());
        }

        if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }
}