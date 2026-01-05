// ============================================================================
// core/tooltip_manager.h
// Manages tooltips for settings UI elements
// Loads from external JSON file (tooltips.json)
// ============================================================================
#pragma once

#include <string>
#include <unordered_map>
#include <fstream>
#include "../diagnostics/logger.h"
#include "../vendor/nlohmann/json.hpp"

// Tab tooltip info loaded from JSON
struct TabTooltip {
    std::string title;        // Tab title (e.g., "Standings")
    std::string tooltip;      // Tab tooltip shown at top of tab content
};

class TooltipManager {
public:
    static TooltipManager& getInstance() {
        static TooltipManager instance;
        return instance;
    }

    // Load tooltips from JSON file in plugins\mxbmrp3_data\tooltips.json
    void load() {
        m_loaded = false;
        std::ifstream file("plugins\\mxbmrp3_data\\tooltips.json");
        if (!file.is_open()) {
            DEBUG_INFO("[TooltipManager] No tooltips.json found (optional)");
            return;
        }

        try {
            nlohmann::json j;
            file >> j;

            if (j.value("version", 0) < 1) {
                DEBUG_INFO("[TooltipManager] Invalid version in tooltips.json");
                return;
            }

            // Parse tab tooltips
            if (j.contains("tabs") && j["tabs"].is_object()) {
                for (auto& [key, value] : j["tabs"].items()) {
                    TabTooltip tab;
                    tab.title = value.value("title", "");
                    tab.tooltip = value.value("tooltip", "");
                    m_tabs[key] = tab;
                }
            }

            // Parse control tooltips
            if (j.contains("controls") && j["controls"].is_object()) {
                for (auto& [key, value] : j["controls"].items()) {
                    if (value.is_string()) {
                        m_controls[key] = value.get<std::string>();
                    }
                }
            }

            m_loaded = true;
            DEBUG_INFO_F("[TooltipManager] Loaded %zu tabs, %zu controls",
                         m_tabs.size(), m_controls.size());

        } catch (const std::exception& e) {
            DEBUG_INFO_F("[TooltipManager] Failed to parse tooltips.json: %s", e.what());
        }
    }

    // Reload tooltips from disk
    void reload() {
        m_tabs.clear();
        m_controls.clear();
        load();
    }

    // Get tab tooltip by tab ID (e.g., "standings", "map")
    const TabTooltip& getTabTooltip(const char* tabId) const {
        auto it = m_tabs.find(tabId);
        return (it != m_tabs.end()) ? it->second : m_emptyTab;
    }

    // Get control tooltip by control ID (e.g., "common.visible", "standings.rows")
    const char* getControlTooltip(const char* controlId) const {
        auto it = m_controls.find(controlId);
        return (it != m_controls.end()) ? it->second.c_str() : "";
    }

    bool isLoaded() const { return m_loaded; }

private:
    TooltipManager() : m_loaded(false) {}
    ~TooltipManager() = default;
    TooltipManager(const TooltipManager&) = delete;
    TooltipManager& operator=(const TooltipManager&) = delete;

    std::unordered_map<std::string, TabTooltip> m_tabs;
    std::unordered_map<std::string, std::string> m_controls;
    TabTooltip m_emptyTab;
    bool m_loaded;
};
