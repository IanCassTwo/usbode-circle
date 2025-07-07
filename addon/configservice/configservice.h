#ifndef CONFIGSERVICE_H
#define CONFIGSERVICE_H

#include <string>
#include <vector> // Included for completeness, though may not be directly used in .h
#include <map>
#include <fatfs/ff.h> // For FRESULT, FIL etc.

class ConfigService {
public:
    ConfigService();
    ~ConfigService();

    // Method to explicitly load configuration
    // Returns true on success, false on failure
    bool LoadConfig();

    // Getter and Setter for displayhat
    std::string GetDisplayHat() const;
    bool SetDisplayHat(const std::string& displayhat);

    // Getter and Setter for screen_timeout
    std::string GetScreenTimeout() const;
    bool SetScreenTimeout(const std::string& timeout);

    // Getter and Setter for logfile
    std::string GetLogFile() const;
    bool SetLogFile(const std::string& logfile);

    // Getter and Setter for default_volume
    std::string GetDefaultVolume() const;
    bool SetDefaultVolume(const std::string& volume);

    // Getter and Setter for sounddev
    std::string GetSoundDevice() const;
    bool SetSoundDevice(const std::string& sounddev);

    // Getter and Setter for loglevel
    std::string GetLogLevel() const;
    bool SetLogLevel(const std::string& loglevel);

    // Getter and Setter for usbspeed
    std::string GetUsbSpeed() const;
    bool SetUsbSpeed(const std::string& usbspeed);

private:
    // Private methods to handle file operations for specific files
    bool ParseConfigFile();
    bool UpdateConfigFile();
    bool ParseCmdlineFile();
    bool UpdateCmdlineFile();

    // Save all configurations. Called by setters.
    // Returns true on success, false on failure.
    bool SaveConfig();

    // Member variables to store configuration data
    std::map<std::string, std::string> m_usbodeConfig;
    std::map<std::string, std::string> m_cmdlineConfig;

    const char* m_configFilePath = "SD:/config.txt";
    const char* m_cmdlineFilePath = "SD:/cmdline.txt";
};

#endif // CONFIGSERVICE_H
