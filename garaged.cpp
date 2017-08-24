#include "garaged.h"
#include <cerrno>
#include <set>
#include <utility>
#include <cstdlib>
#include <ctime>
#include <iomanip>

#ifndef EMU
#  include <unistd.h>
#  include <sys/sysinfo.h>
#  include <wiringPi.h>
#  define Z_system system
#  define Z_sysinfo sysinfo
#else
#  include "Emu.h"
#endif

using namespace std;

static bool IsButtonPressed()
{
    return (digitalRead(PN_Button) == LOW);
}

static bool IsGatePressed()
{
    return (digitalRead(PN_Gate) == LOW);
}

static std::ostream& WriteCurTime(std::ostream& s)
{
    time_t now = time(nullptr);
    tm now_tm;
#   ifdef _WIN32
    gmtime_s(&now_tm, &now);
#   else
    gmtime_r(&now, &now_tm);
#   endif
    s << put_time(&now_tm, "[%Y-%m-%d %H:%M:%S UTC]");
    return s;
}

static void WriteSysInfo(std::ostream& s)
{
    if (s.good())
    {
        WriteCurTime(s);
        struct Z_sysinfo si;
        if (Z_sysinfo(&si) == 0)
        {
            int updays = si.uptime / 86400;
            int uphours = si.uptime % 86400 / 3600;
            int upminutes = si.uptime % 3600 / 60;
            int upsecs = si.uptime % 60;
            s << " System info:\n" <<
                "Uptime: " << updays << "d " << uphours << "h " << upminutes << "m " << upsecs << "s\n" <<
                "Load Avgs: " << si.loads[0] << ":1m " << si.loads[1] << ":5m " << si.loads[2] << ":15m\n" <<
                "RAM: " << si.totalram << ":tot " << si.freeram << ":fr " << si.sharedram << ":shrd " << si.bufferram << ":buf\n" <<
                "Processes: " << si.procs << endl;
        }
        else
        {
            s << " Unable to retrieve system info" << endl;
        }
    }
}


template<typename... T>
void Garaged::Log(const T&... args)
{
    if (_log.good())
    {
        _log << WriteCurTime << ' ';
        Log2(args...);
    }
}

void Garaged::Log2()
{
    _log << endl;
}

template<typename T1, typename... T>
void Garaged::Log2(const T1& arg1, const T&... args)
{
    _log << arg1;
    Log2(args...);
}

Garaged& Garaged::Instance()
{
    static Garaged garaged;
    return garaged;
}

void Garaged::Init()
{
    Log("Starting garaged...");
    wiringPiSetup();
    pinMode(PN_Relay, OUTPUT);
    pinMode(PN_Button, INPUT);
    pinMode(PN_Gate, INPUT);
    pinMode(PN_InternalLed, OUTPUT);
    pinMode(PN_ExternalLed, OUTPUT);
    pullUpDnControl(PN_Button, PUD_OFF);
    pullUpDnControl(PN_Gate, PUD_OFF);
    digitalWrite(PN_Relay, LOW);
    digitalWrite(PN_InternalLed, LOW);
    digitalWrite(PN_ExternalLed, LOW);
    static Garaged* gGaraged = this;
    wiringPiISR(PN_Button, INT_EDGE_BOTH, []
    {
        gGaraged->Q().PlanEvent(ET_Button, ReactDelay, true);
    });
    wiringPiISR(PN_Gate, INT_EDGE_BOTH, []
    {
        gGaraged->Q().PlanEvent(ET_Gate, ReactDelay, true);
    });

    Q().PlanEvent(Event(ET_Blink, 1));
    Q().PlanEvent(ET_Button);
    Q().PlanEvent(ET_Gate);
    Q().PlanEvent(ET_WriteStats);
}

void Garaged::ControlLight(LightMode newMode)
{
    if (newMode != _lightMode)
    {
        digitalWrite(PN_ExternalLed, LOW);
        if (_lightMode == LM_AlmostOff)
        {
            Q().DeleteEvents(ET_BlinkExternal);
            Q().DeleteEvents(ET_LightFinalOff);
        }

        if (_lightMode == LM_Off || newMode == LM_Off)
        {
            Log("Control Light: ", (newMode != LM_Off) ? "On" : "Off");
            digitalWrite(PN_Relay, (newMode != LM_Off) ? HIGH : LOW);
        }
        _lightMode = newMode;

        if (newMode == LM_On)
        {
            _lightOnTime = Clock::now();
            Q().PlanEvent(ET_LightTooLong, LightTooLongTimeout);
            Q().PlanEvent(ET_DisplayTimeLeft, DisplayTimeLeftTime);
        }
        else
        {
            Q().DeleteEvents(ET_LightTooLong);
            Q().DeleteEvents(ET_DisplayTimeLeft);
            Q().DeleteEvents(ET_DisplayTimeLeftBlink);

            if (newMode == LM_AlmostOff)
            {
                Q().PlanEvent(ET_LightFinalOff, LightFinalOffTimeout);
                Q().PlanEvent(Event(ET_BlinkExternal, 1), LightTimeoutBlink);
            }
        }
    }
}

void Garaged::SetLogFileName(const char* filename)
{
    _log.open(filename, _log.binary | _log.app | _log.out);
}

void Garaged::Exec()
{
    Init();
    for (;;)
    {
        Event evt = Q().WaitEvent();
        if (evt.Type() == ET_Blink)
        {
            bool blink = (evt.Data() != 0 ? true : false);
            digitalWrite(PN_InternalLed, blink ? HIGH : LOW);
            Q().PlanEvent(Event(ET_Blink, !blink), blink ? BlinkOnTime : BlinkOffTime);
        }
        else if (evt.Type() == ET_Button)
        {
            if (_buttonPressed != IsButtonPressed())
            {
                _buttonPressed = !_buttonPressed;
                if (_buttonPressed)
                {
                    Log("Button pressed");
                    _buttonPressTime = Clock::now();
                    Q().PlanEvent(ET_Halt, ButtonHaltTime);
                }
                else
                {
                    Log("Button released");
                    Q().DeleteEvents(ET_Halt);
                    Duration dur = Clock::now() - _buttonPressTime;
                    if (dur > ButtonContinueTime)
                    {
                        if (_lightMode != LM_Off)
                        {
                            ControlLight(LM_AlmostOff);
                            ControlLight(LM_On);
                        }
                    }
                    else
                    {
                        if (_lightMode != LM_On)
                        {
                            ControlLight(LM_On);
                        }
                        else
                        {
                            ControlLight(LM_Off);
                        }
                    }

                }
            }
        }
        else if (evt.Type() == ET_Gate)
        {
            if (_gatePressed != IsGatePressed())
            {
                _gatePressed = !_gatePressed;
                if (_gatePressed)
                {
                    Log("Gate button pressed");
                    _gatePressTime = Clock::now();
                    _gatePressInstantAction = (_lightMode == LM_Off);
                    if(_gatePressInstantAction)
                    {
                        ControlLight(LM_On);
                    }
                }
                else
                {
                    Log("Gate button released");
                    if (!_gatePressInstantAction)
                    {

                        Duration dur = Clock::now() - _gatePressTime;
                        if (dur > ButtonContinueTime)
                        {
                            if (_lightMode != LM_Off)
                            {
                                ControlLight(LM_AlmostOff);
                                ControlLight(LM_On);
                            }
                        }
                        else
                        {
                            if (_lightMode != LM_On)
                            {
                                ControlLight(LM_On);
                            }
                            else
                            {
                                ControlLight(LM_Off);
                            }
                        }
                    }
                }
            }
        }
        else if (evt.Type() == ET_LightFinalOff)
        {
            Log("Light timed out");
            ControlLight(LM_Off);
        }
        else if (evt.Type() == ET_BlinkExternal)
        {
            bool blink = (evt.Data() != 0 ? true : false);
            digitalWrite(PN_ExternalLed, blink ? HIGH : LOW);
            Q().PlanEvent(Event(ET_BlinkExternal, !blink), LightTimeoutBlink);
        }
        else if (evt.Type() == ET_LightTooLong)
        {
            Log("Light almost off");
            ControlLight(LM_AlmostOff);
        }
        else if (evt.Type() == ET_Halt)
        {
            Log("Initiating reboot");
            digitalWrite(PN_Relay, LOW);
            digitalWrite(PN_ExternalLed, HIGH);
            digitalWrite(PN_InternalLed, HIGH);
            int ret = Z_system("reboot");
            Log("Reboot returned ", ret, ". Goodbye.");
            break;
        }
        else if (evt.Type() == ET_WriteStats)
        {
            WriteSysInfo(_log);
            Q().PlanEvent(ET_WriteStats, WriteStatsTime);
        }
        else if (evt.Type() == ET_DisplayTimeLeft)
        {
            Duration lightOnDuration = Clock::now() - _lightOnTime;
            auto ticks = lightOnDuration / DisplayTimeLeftPeriod;
            digitalWrite(PN_ExternalLed, HIGH);
            Q().PlanEvent(Event(ET_DisplayTimeLeftBlink, uint32_t(ticks * 2)), DisplayTimeLeftBlinkOnTime);
        }
        else if (evt.Type() == ET_DisplayTimeLeftBlink)
        {
            if (evt.Data() > 0)
            {
                bool blinkOn = ((evt.Data() & 1) != 0);
                digitalWrite(PN_ExternalLed, blinkOn ? HIGH : LOW);
                Duration dur = blinkOn ? DisplayTimeLeftBlinkOnTime : DisplayTimeLeftBlinkOffTime;
                Q().PlanEvent(Event(ET_DisplayTimeLeftBlink, evt.Data() - 1), dur);
            }
            else
            {
                digitalWrite(PN_ExternalLed, LOW);
                Q().PlanEvent(ET_DisplayTimeLeft, DisplayTimeLeftTime);
            }
        }
    }
}

