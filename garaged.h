#ifndef GUARD_GARAGED_H
#define GUARD_GARAGED_H
#include "events.h"
#include <fstream>

const int PN_Relay = 6;
const int PN_Button = 30;
const int PN_Gate = 31;
const int PN_InternalLed = 21;
const int PN_ExternalLed = 24;

const Duration ReactDelay = std::chrono::milliseconds(100);
const Duration BlinkOnTime = std::chrono::milliseconds(500);
const Duration BlinkOffTime = std::chrono::milliseconds(1500);
const Duration LightFinalOffTimeout = std::chrono::seconds(12);
const Duration LightTimeoutBlink = std::chrono::milliseconds(300);
const Duration LightTooLongTimeout = std::chrono::minutes(20);
const Duration ButtonHaltTime = std::chrono::seconds(7);
const Duration ButtonContinueTime = std::chrono::milliseconds(1200);
const Duration WriteStatsTime = std::chrono::hours(4);
const Duration DisplayTimeLeftTime = std::chrono::milliseconds(2700);
const Duration DisplayTimeLeftBlinkOnTime = std::chrono::milliseconds(70);
const Duration DisplayTimeLeftBlinkOffTime = std::chrono::milliseconds(250);
const Duration DisplayTimeLeftPeriod = std::chrono::minutes(4);

class Garaged
{
protected:
    Garaged() = default;

public:
    enum LightMode
    {
        LM_Off,
        LM_On,
        LM_AlmostOff,
    };

    static Garaged& Instance();

    EventQueue& Q() { return _q; }

    void SetLogFileName(const char* filename);

    void Exec();
    
private:
    template<typename... T>
    void Log(const T&... args);
    void Log2();
    template<typename T1, typename... T>
    void Log2(const T1& arg1, const T&... args);
    
    void Init();

    void ControlLight(LightMode newMode);

    EventQueue _q;
    bool _gateOpen = false;
    bool _buttonPressed = false;
    bool _skipGate = false;
    LightMode _lightMode = LM_Off;
    Time _lightOnTime = Time();
    Time _buttonPressTime = Time();
    std::ofstream _log;
};

#endif