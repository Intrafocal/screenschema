// screenschema sim — esp-brookesia stub
// Provides just enough of the PhoneApp interface for SSAppBase to compile.
#pragma once

class ESP_Brookesia_PhoneApp {
public:
    ESP_Brookesia_PhoneApp(const char* /*name*/, const void* /*icon*/ = nullptr, bool /*default_screen*/ = true) {}
    virtual ~ESP_Brookesia_PhoneApp() = default;

protected:
    void notifyCoreClosed() {}

    virtual bool init()  { return true; }
    virtual bool run()   { return true; }
    virtual bool pause() { return true; }
    virtual bool close() { return true; }
    virtual bool back()  { return true; }
};
