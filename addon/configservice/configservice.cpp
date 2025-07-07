#include "configservice.h"
#include <circle/logger.h>
#include <circle/util.h> // For CString, if needed, or other utils (though mostly std:: used here)
#include <sstream>       // For std::istringstream
#include <fstream>       // For std::ifstream, std::ofstream (though we use FatFs for actual I/O)
#include <vector>
#include <algorithm> // For std::remove (if needed, not directly used in this impl)

// Define LOGMODULE if not already defined globally for this addon
#ifndef LOGMODULE
    #define LOGMODULE "configservice"
#endif
// Note: Actual Circle logging (LOGNOTE, LOGDBG etc.) needs proper setup if used.
// For simplicity, using placeholders or assuming it's available.

ConfigService::ConfigService() {
    LOGNOTE("ConfigService initializing...");
    if (!LoadConfig()) {
        LOGERR("Failed to load configuration during ConfigService construction.");
    }
}

ConfigService::~ConfigService() {
    LOGNOTE("ConfigService shutting down.");
    // Maps will clean themselves up.
}

bool ConfigService::LoadConfig() {
    LOGDBG("Loading all configurations...");
    m_usbodeConfig.clear(); // Ensure maps are empty before loading
    m_cmdlineConfig.clear();

    bool configSuccess = ParseConfigFile();
    bool cmdlineSuccess = ParseCmdlineFile();
    if (!configSuccess) {
        LOGERR("Failed to parse %s", m_configFilePath);
    }
    if (!cmdlineSuccess) {
        LOGERR("Failed to parse %s", m_cmdlineFilePath);
    }
    return configSuccess && cmdlineSuccess;
}

bool ConfigService::ParseConfigFile() {
    FIL file;
    FRESULT result = f_open(&file, m_configFilePath, FA_READ);
    if (result != FR_OK) {
        LOGWARN("Failed to open %s for reading: %d. This might be normal if the file doesn't exist yet.", m_configFilePath, result);
        return false;
    }

    LOGINF("Successfully opened %s for reading.", m_configFilePath);
    char line_buf[256]; // Max line length for config.txt
    bool in_usbode_section = false;

    while (f_gets(line_buf, sizeof(line_buf), &file)) {
        std::string str_line(line_buf);
        // Remove trailing newline/carriage return
        str_line.erase(str_line.find_last_not_of("\r\n") + 1);

        if (str_line.rfind("[usbode]", 0) == 0) {
            in_usbode_section = true;
            continue;
        } else if (str_line.rfind("[", 0) == 0) {
            in_usbode_section = false;
            continue;
        }

        if (in_usbode_section) {
            size_t pos = str_line.find('=');
            if (pos != std::string::npos) {
                std::string key = str_line.substr(0, pos);
                std::string value = str_line.substr(pos + 1);
                m_usbodeConfig[key] = value;
                LOGDBG("Loaded from config.txt: %s = %s", key.c_str(), value.c_str());
            }
        }
    }

    f_close(&file);
    LOGINF("Finished parsing %s.", m_configFilePath);
    return true;
}

bool ConfigService::ParseCmdlineFile() {
    FIL file;
    FRESULT result = f_open(&file, m_cmdlineFilePath, FA_READ);
    if (result != FR_OK) {
        LOGWARN("Failed to open %s for reading: %d. This might be normal if the file doesn't exist yet.", m_cmdlineFilePath, result);
        return false;
    }

    LOGINF("Successfully opened %s for reading.", m_cmdlineFilePath);
    char line_buf[512]; // cmdline can be a single long line
    if (f_gets(line_buf, sizeof(line_buf), &file)) {
        std::string str_line(line_buf);
        str_line.erase(str_line.find_last_not_of("\r\n") + 1);

        std::istringstream iss(str_line);
        std::string param;

        while (iss >> param) {
            size_t pos = param.find('=');
            if (pos != std::string::npos) {
                std::string key = param.substr(0, pos);
                std::string value = param.substr(pos + 1);
                m_cmdlineConfig[key] = value;
                LOGDBG("Loaded from cmdline.txt: %s = %s", key.c_str(), value.c_str());
            } else {
                m_cmdlineConfig[param] = "true"; // Store parameters without values as "true"
                LOGDBG("Loaded from cmdline.txt: %s = true", param.c_str());
            }
        }
    }

    f_close(&file);
    LOGINF("Finished parsing %s.", m_cmdlineFilePath);
    return true;
}

bool ConfigService::SaveConfig() {
    LOGDBG("Saving all configurations...");
    bool configSuccess = UpdateConfigFile();
    bool cmdlineSuccess = UpdateCmdlineFile();
     if (!configSuccess) {
        LOGERR("Failed to update %s", m_configFilePath);
    }
    if (!cmdlineSuccess) {
        LOGERR("Failed to update %s", m_cmdlineFilePath);
    }
    return configSuccess && cmdlineSuccess;
}

bool ConfigService::UpdateConfigFile() {
    std::vector<std::string> lines;
    FIL read_file;
    FRESULT result = f_open(&read_file, m_configFilePath, FA_READ);

    bool file_exists = (result == FR_OK);
    bool in_usbode_section = false;
    std::map<std::string, bool> params_written; // To track which params from m_usbodeConfig are handled

    // Initialize all managed params as not written
    for (const auto& pair : m_usbodeConfig) {
        params_written[pair.first] = false;
    }

    if (file_exists) {
        LOGDBG("Reading existing %s to update.", m_configFilePath);
        char line_buf[256];
        while (f_gets(line_buf, sizeof(line_buf), &read_file)) {
            std::string str_line(line_buf);
            str_line.erase(str_line.find_last_not_of("\r\n") + 1);

            if (str_line.rfind("[usbode]", 0) == 0) {
                in_usbode_section = true;
                lines.push_back(str_line); // Add [usbode] header
                // Defer writing new params until after processing existing keys in this section
                continue;
            } else if (str_line.rfind("[", 0) == 0) { // Another section starts
                if (in_usbode_section) { // Leaving usbode section
                    // Add any new/unwritten params from m_usbodeConfig to the end of [usbode]
                    for (const auto& pair : m_usbodeConfig) {
                        if (!params_written[pair.first] && !pair.second.empty()) {
                            lines.push_back(pair.first + "=" + pair.second);
                            params_written[pair.first] = true; // Mark as written
                            LOGDBG("Added new param at end of usbode section: %s=%s", pair.first.c_str(), pair.second.c_str());
                        }
                    }
                }
                in_usbode_section = false;
                lines.push_back(str_line); // Add the new section header
                continue;
            }

            if (in_usbode_section) {
                size_t pos = str_line.find('=');
                if (pos != std::string::npos) {
                    std::string key_from_file = str_line.substr(0, pos);

                    auto it = m_usbodeConfig.find(key_from_file);
                    if (it != m_usbodeConfig.end()) { // This key is managed by us
                        if (!it->second.empty()) { // If value is not empty, write it
                            lines.push_back(it->first + "=" + it->second);
                            LOGDBG("Updated param in config.txt: %s=%s", it->first.c_str(), it->second.c_str());
                        } else {
                             LOGDBG("Removed param from config.txt (empty value): %s", it->first.c_str());
                             // Line is skipped, effectively removing it
                        }
                        params_written[it->first] = true; // Mark as handled
                    } else { // Key not managed by us (or not in current m_usbodeConfig), keep it
                        lines.push_back(str_line);
                    }
                } else { // Not a key-value line within usbode (e.g. a comment), keep it
                    lines.push_back(str_line);
                }
            } else { // Not in usbode section, keep the line
                lines.push_back(str_line);
            }
        }
        f_close(&read_file);
    }

    // If [usbode] section was not found in an existing file, or if file didn't exist at all
    bool usbode_header_present = false;
    for(const auto& l : lines) {
        if (l.rfind("[usbode]", 0) == 0) {
            usbode_header_present = true;
            break;
        }
    }
    if (!usbode_header_present) {
        LOGDBG("[usbode] section not found in %s or file doesn't exist. Creating section.", m_configFilePath);
        lines.push_back("[usbode]"); // Ensure the section header exists
    }

    // Add any remaining (new) params from m_usbodeConfig that weren't processed
    // This typically happens if they are new keys or if the [usbode] section was just added.
    // Find the [usbode] section to insert new keys, or add them at the end if it's the last section.
    size_t usbode_section_end_idx = lines.size();
    bool found_usbode_for_new_keys = false;
    for(size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].rfind("[usbode]", 0) == 0) {
            found_usbode_for_new_keys = true;
            // Look for the end of this section or end of file
            usbode_section_end_idx = lines.size(); // Default to end of all lines
            for(size_t j = i + 1; j < lines.size(); ++j) {
                if (lines[j].rfind("[", 0) == 0) { // Next section found
                    usbode_section_end_idx = j;
                    break;
                }
            }
            break;
        }
    }

    if(found_usbode_for_new_keys) {
        std::vector<std::string> new_keys_to_insert;
        for (const auto& pair : m_usbodeConfig) {
            if (!params_written[pair.first] && !pair.second.empty()) {
                new_keys_to_insert.push_back(pair.first + "=" + pair.second);
                LOGDBG("Queued new param for [usbode] section: %s=%s", pair.first.c_str(), pair.second.c_str());
            }
        }
        if (!new_keys_to_insert.empty()) {
            lines.insert(lines.begin() + usbode_section_end_idx, new_keys_to_insert.begin(), new_keys_to_insert.end());
        }
    } else if (!m_usbodeConfig.empty() && !usbode_header_present) {
        // This case should be rare if [usbode] header logic above works, but as a fallback:
        // If no [usbode] header was ever added (e.g. empty file, no config)
        // and we have usbode configs, add them all.
        LOGDBG("Fallback: Adding all usbode params as no section was found/created prior to this check.");
        lines.push_back("[usbode]");
        for (const auto& pair : m_usbodeConfig) {
            if (!pair.second.empty()) {
                 lines.push_back(pair.first + "=" + pair.second);
            }
        }
    }

    FIL write_file;
    result = f_open(&write_file, m_configFilePath, FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) {
        LOGERR("Failed to open %s for writing: %d", m_configFilePath, result);
        return false;
    }

    LOGINF("Writing updated configuration to %s", m_configFilePath);
    for (const auto& l : lines) {
        UINT bytes_written;
        std::string line_to_write = l + "\n"; // FatFs f_write doesn't add newlines automatically
        result = f_write(&write_file, line_to_write.c_str(), line_to_write.length(), &bytes_written);
        if (result != FR_OK || bytes_written != line_to_write.length()) {
            LOGERR("Failed to write line to %s: %s (Error: %d, Written: %u)", m_configFilePath, l.c_str(), result, bytes_written);
            f_close(&write_file);
            return false;
        }
    }

    f_close(&write_file);
    LOGINF("Successfully wrote updates to %s.", m_configFilePath);
    return true;
}

bool ConfigService::UpdateCmdlineFile() {
    std::string cmdline_content;
    for (const auto& pair : m_cmdlineConfig) {
        if (!cmdline_content.empty()) {
            cmdline_content += " ";
        }
        if (pair.second == "true") { // Parameter without value
            cmdline_content += pair.first;
        } else if (!pair.second.empty()) { // Parameter with value
            cmdline_content += pair.first + "=" + pair.second;
        }
        // If value is empty (but not "true"), it's effectively removed by not being added here.
    }

    FIL file;
    FRESULT result = f_open(&file, m_cmdlineFilePath, FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) {
        LOGERR("Failed to open %s for writing: %d", m_cmdlineFilePath, result);
        return false;
    }

    LOGINF("Writing updated configuration to %s", m_cmdlineFilePath);
    UINT bytes_written;
    // cmdline.txt usually has one line then a newline
    std::string content_with_newline = cmdline_content + "\n";
    result = f_write(&file, content_with_newline.c_str(), content_with_newline.length(), &bytes_written);
    if (result != FR_OK || bytes_written != content_with_newline.length()) {
        LOGERR("Failed to write to %s (Error: %d, Written: %u)", m_cmdlineFilePath, result, bytes_written);
        f_close(&file);
        return false;
    }

    f_close(&file);
    LOGINF("Successfully wrote updates to %s.", m_cmdlineFilePath);
    return true;
}

// --- Getters ---
std::string ConfigService::GetDisplayHat() const {
    auto it = m_usbodeConfig.find("displayhat");
    return (it != m_usbodeConfig.end()) ? it->second : "none"; // Default to "none"
}

std::string ConfigService::GetScreenTimeout() const {
    auto it = m_usbodeConfig.find("screen_timeout");
    return (it != m_usbodeConfig.end()) ? it->second : "5"; // Default to "5"
}

std::string ConfigService::GetLogFile() const {
    auto it = m_usbodeConfig.find("logfile");
    // Return empty string if not found, which can mean "disabled"
    return (it != m_usbodeConfig.end()) ? it->second : "";
}

std::string ConfigService::GetDefaultVolume() const {
    auto it = m_usbodeConfig.find("default_volume");
    return (it != m_usbodeConfig.end()) ? it->second : "255"; // Default to "255"
}

std::string ConfigService::GetSoundDevice() const {
    auto it = m_cmdlineConfig.find("sounddev");
    return (it != m_cmdlineConfig.end()) ? it->second : "sndpwm"; // Default to "sndpwm"
}

std::string ConfigService::GetLogLevel() const {
    auto it = m_cmdlineConfig.find("loglevel");
    return (it != m_cmdlineConfig.end()) ? it->second : "4"; // Default to "4"
}

std::string ConfigService::GetUsbSpeed() const {
    auto it = m_cmdlineConfig.find("usbspeed");
    if (it != m_cmdlineConfig.end() && it->second == "full") {
        return "full";
    }
    // Absence of "usbspeed=full" (or any other value) means high speed.
    return "high";
}

// --- Setters ---
// Each setter updates the internal map and then calls SaveConfig.
// Returns true if save was successful.

bool ConfigService::SetDisplayHat(const std::string& value) {
    LOGDBG("Setting displayhat to: %s", value.c_str());
    m_usbodeConfig["displayhat"] = value;
    return SaveConfig();
}

bool ConfigService::SetScreenTimeout(const std::string& value) {
    LOGDBG("Setting screen_timeout to: %s", value.c_str());
    m_usbodeConfig["screen_timeout"] = value;
    return SaveConfig();
}

bool ConfigService::SetLogFile(const std::string& value) {
    LOGDBG("Setting logfile to: %s", value.c_str());
    // If value is empty, it effectively means "remove" or "disable" for logfile.
    // The UpdateConfigFile logic will skip writing it if the value is empty.
    m_usbodeConfig["logfile"] = value;
    return SaveConfig();
}

bool ConfigService::SetDefaultVolume(const std::string& value) {
    LOGDBG("Setting default_volume to: %s", value.c_str());
    m_usbodeConfig["default_volume"] = value;
    return SaveConfig();
}

bool ConfigService::SetSoundDevice(const std::string& value) {
    LOGDBG("Setting sounddev to: %s", value.c_str());
    if (value.empty()) {
        m_cmdlineConfig.erase("sounddev"); // Remove parameter if value is empty
    } else {
        m_cmdlineConfig["sounddev"] = value;
    }
    return SaveConfig();
}

bool ConfigService::SetLogLevel(const std::string& value) {
    LOGDBG("Setting loglevel to: %s", value.c_str());
     if (value.empty()) {
        m_cmdlineConfig.erase("loglevel"); // Remove parameter if value is empty
    } else {
        m_cmdlineConfig["loglevel"] = value;
    }
    return SaveConfig();
}

bool ConfigService::SetUsbSpeed(const std::string& value) {
    LOGDBG("Setting usbspeed to: %s", value.c_str());
    if (value == "full") {
        m_cmdlineConfig["usbspeed"] = "full";
    } else {
        // Any other value (e.g., "high" or empty string) means remove the "usbspeed=full" parameter.
        // This makes the system default to high speed.
        m_cmdlineConfig.erase("usbspeed");
    }
    return SaveConfig();
}
