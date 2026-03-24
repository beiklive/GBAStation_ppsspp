/// @file PpssppConfig.h
/// @brief PPSSPP core configuration parser for the external NRO
#pragma once

#include <string>
#include <map>
#include <fstream>
#include <cstring>
#include "TicoConfig.h"
#include "TicoLogger.h"

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

// Forward declare nlohmann::json; include in .cpp
#include <json.hpp>

/// @brief Configuration reader/writer for PPSSPP core options
/// Reads from sdmc:/tico/config/cores/ppsspp.jsonc
class PpssppConfig {
public:
    static PpssppConfig& Instance() {
        static PpssppConfig inst;
        return inst;
    }

    void Load() {
        if (m_loaded) return;

        const char* configPath = TicoConfig::CONFIG_PATH;

        std::ifstream f(configPath);
        if (f.good()) {
            m_config = nlohmann::json::parse(f, nullptr, false, true);
            if (m_config.is_discarded()) {
                LOG_ERROR("CONFIG", "[PpssppConfig] Corrupt config detected. Recreating...");
                f.close();
                remove(configPath);
                CreateDefaultConfig(configPath);
                // Retry
                std::ifstream f2(configPath);
                if (f2.good()) {
                    m_config = nlohmann::json::parse(f2, nullptr, false, true);
                    if (m_config.is_discarded()) {
                        LOG_ERROR("CONFIG", "[PpssppConfig] Default config failed to parse!");
                        m_config = nlohmann::json::object();
                    }
                }
            }
            LOG_INFO("CONFIG", "[PpssppConfig] Loaded config from %s", configPath);
            m_loaded = true;
        } else {
            CreateDefaultConfig(configPath);
            std::ifstream f2(configPath);
            if (f2.good()) {
                m_config = nlohmann::json::parse(f2, nullptr, false, true);
                if (m_config.is_discarded()) {
                    LOG_ERROR("CONFIG", "[PpssppConfig] Default config failed to parse!");
                    m_config = nlohmann::json::object();
                }
                m_loaded = true;
            }
        }

        // Build the flat map for fast lookup
        BuildOptionsMap();
    }

    void Unload() {
        if (m_loaded) {
            m_config.clear();
            m_options.clear();
            m_loaded = false;
            LOG_INFO("CONFIG", "[PpssppConfig] Unloaded config");
        }
    }

    /// @brief Handle RETRO_ENVIRONMENT_GET_VARIABLE for ppsspp_ prefixed keys
    bool HandleGetVariable(struct retro_variable* var) {
        if (!var || !var->key) return false;

        if (strncmp(var->key, "ppsspp_", 7) == 0) {
            auto it = m_options.find(var->key);
            if (it != m_options.end()) {
                var->value = it->second.c_str();
                return true;
            }
        }

        return false;
    }

    /// @brief Get a config value by key
    std::string GetValue(const std::string& key, const std::string& defaultVal = "") {
        auto it = m_options.find(key);
        if (it != m_options.end()) {
            return it->second;
        }
        return defaultVal;
    }

    /// @brief Set a config value
    void SetValue(const std::string& key, const std::string& value) {
        m_config[key] = value;
        m_options[key] = value;
    }

    /// @brief Save config back to disk
    void Save() {
        if (!m_loaded) return;

        const char* configPath = TicoConfig::CONFIG_PATH;
        std::ofstream out(configPath);
        if (out.good()) {
            out << m_config.dump(4);
            out.close();
            LOG_INFO("CONFIG", "[PpssppConfig] Saved config to %s", configPath);
        } else {
            LOG_ERROR("CONFIG", "[PpssppConfig] Failed to save config to %s", configPath);
        }
    }

    /// @brief Get the full options map (for TicoCore's GET_VARIABLE)
    const std::map<std::string, std::string>& GetOptionsMap() const { return m_options; }

private:
    PpssppConfig() = default;

    void BuildOptionsMap() {
        m_options.clear();
        for (auto& el : m_config.items()) {
            if (el.value().is_string()) {
                m_options[el.key()] = el.value().get<std::string>();
            } else if (el.value().is_boolean()) {
                m_options[el.key()] = el.value().get<bool>() ? "true" : "false";
            } else if (el.value().is_number_integer()) {
                m_options[el.key()] = std::to_string(el.value().get<int>());
            } else if (el.value().is_number_float()) {
                m_options[el.key()] = std::to_string(el.value().get<float>());
            }
        }
    }

    void CreateDefaultConfig(const char* configPath) {
        LOG_INFO("CONFIG", "[PpssppConfig] Creating default config at %s", configPath);

#ifdef __SWITCH__
        // Ensure directory exists
        mkdir("sdmc:/tico", 0777);
        mkdir("sdmc:/tico/config", 0777);
        mkdir("sdmc:/tico/config/cores", 0777);
#endif

        std::ofstream out(configPath);
        if (out.good()) {
            out << R"json({
    "ppsspp_cpu_core": "JIT",
    "ppsspp_fast_memory": "enabled",
    "ppsspp_ignore_bad_memory_access": "enabled",
    "ppsspp_io_timing_method": "Fast",
    "ppsspp_force_lag_sync": "disabled",
    "ppsspp_locked_cpu_speed": "0",
    "ppsspp_cache_iso": "disabled",
    "ppsspp_cheats": "disabled",
    "ppsspp_psp_model": "psp_2000_3000",
    "ppsspp_button_preference": "Cross",
    "ppsspp_internal_resolution": "480x272",
    "ppsspp_software_rendering": "disabled",
    "ppsspp_rendering_mode": "buffered",
    "ppsspp_gpu_hardware_transform": "enabled",
    "ppsspp_texture_filtering": "Auto",
    "ppsspp_texture_anisotropic_filtering": "Off",
    "ppsspp_lower_resolution_for_effects": "Off",
    "ppsspp_texture_deposterize": "disabled",
    "ppsspp_texture_scaling_type": "xbrz",
    "ppsspp_texture_scaling_level": "1",
    "ppsspp_texture_replacement": "enabled",
    "ppsspp_skip_buffer_effects": "disabled",
    "ppsspp_frameskip": "0",
    "ppsspp_auto_frameskip": "disabled",
    "ppsspp_frame_duplication": "disabled",
    "ppsspp_detect_vsync_swap_interval": "disabled",
    "ppsspp_inflight_frames": "Up to 2",
    "ppsspp_analog_is_circular": "disabled",
    "ppsspp_analog_deadzone": "0.15",
    "ppsspp_analog_sensitivity": "1.10",
    "ppsspp_language": "Automatic",
    "ppsspp_memstick_inserted": "enabled",
    "ppsspp_cropto16x9": "disabled",
    "ppsspp_block_transfer_gpu": "enabled",
    "ppsspp_disable_range_culling": "disabled",
    "display_mode": "Display",
    "display_size": "16:9",
    "integer_scale": "Auto"
})json";
            out.close();
        }
    }

    nlohmann::json m_config;
    std::map<std::string, std::string> m_options;
    bool m_loaded = false;
};
