// screenschema sim — esp-brookesia stub
// Provides just enough of the PhoneApp interface for SSAppBase to compile.
#pragma once

class ESP_Brookesia_Phone {};  // opaque — ss_shell.hpp names the type

class ESP_Brookesia_PhoneApp {
public:
    ESP_Brookesia_PhoneApp(const char* /*name*/, const void* /*icon*/ = nullptr, bool /*default_screen*/ = true) {}
    virtual ~ESP_Brookesia_PhoneApp() = default;

    int getId() const { return 0; }  // ss_shell.cpp calls it

protected:
    void notifyCoreClosed() {}

    virtual bool init()   { return true; }
    virtual bool run()    { return true; }
    virtual bool resume() { return true; }  // matches SSAppBase override
    virtual bool pause()  { return true; }
    virtual bool close()  { return true; }
    virtual bool back()   { return true; }
};
