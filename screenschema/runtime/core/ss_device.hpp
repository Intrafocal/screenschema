#pragma once
#include <string>

class SSDevice {
public:
    // Call once from app_main before anything else.
    // If id is empty, derives from WiFi MAC: "hub-AABBCC" (last 3 bytes).
    static void init(const char* id, const char* location, const char* friendly_name);

    static const std::string& id();
    static const std::string& location();
    static const std::string& friendlyName();

private:
    static std::string id_;
    static std::string location_;
    static std::string friendly_name_;
};
