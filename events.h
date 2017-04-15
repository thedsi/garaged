#ifndef GUARD_EVENTS_H
#define GUARD_EVENTS_H

#include <cstdint>
#include <type_traits>
#include <chrono>
#include <cassert>
#include <set>
#include <mutex>
#include <condition_variable>

using Clock = std::conditional_t<std::chrono::high_resolution_clock::is_steady, std::chrono::high_resolution_clock, std::chrono::steady_clock>;
using Time = Clock::time_point;
using Duration = Clock::duration;

using EventId = std::uint64_t;

enum EventType
{
    ET_Null,
    ET_Blink,
    ET_Button,
    ET_Gate,
    ET_LightFinalOff,
    ET_BlinkExternal,
    ET_LightTooLong,
    ET_Halt,
    ET_WriteStats,
    ET_DisplayTimeLeft,
    ET_DisplayTimeLeftBlink,
};

enum EventAction
{
    EA_None,
    EA_Wait,
    EA_New,
    EA_Plan,
    EA_Delete,
    EA_Dispatch,
};

inline const char* GetEventName(EventType evt)
{
    switch (evt)
    {
    case ET_Null:            return "Null";
    case ET_Blink:           return "Blink";
    case ET_Button:          return "Button";
    case ET_Gate:            return "Gate";
    case ET_LightFinalOff:    return "LightTimeout";
    case ET_BlinkExternal:   return "BlinkExternal";
    case ET_LightTooLong:    return "LightTooLong";
    case ET_Halt:            return "Halt";
    case ET_WriteStats:      return "WriteStats";
    case ET_DisplayTimeLeft: return "DisplayTimeLeft";
    case ET_DisplayTimeLeftBlink: return "DisplayTimeLeftBlink";
    }
    assert(0);
    return nullptr;
}

class Event
{
public:
    EventType Type() const { return _type; }
    std::uint32_t Data() const { return _data; }

    Event(EventType type = ET_Null, std::uint32_t data = 0) : _type(type), _data(data) {}
private:
    EventType _type;
    std::uint32_t _data;
};

class EventQueue
{
private:
    
    struct Entry
    {
        Event event;
        Time time;
        EventId num;

        Entry(Event event, Time time, EventId num)
            : event(event), time(time), num(num) {}

        bool operator<(const Entry& rhs) const
        {
            return time < rhs.time || (time == rhs.time && num < rhs.num);
        }
    };
    friend void Z_EventNotify(EventAction, const EventQueue::Entry*);

public:
    void PlanEvent(Event event, Duration duration, bool deletePrevious = false);

    void PlanEvent(Event event, Time time = Time(), bool deletePrevious = false);

    Event WaitEvent();

    void DeleteEvents(EventType type);

private:
    void EraseFromQueueNotSync(EventType type);

    std::mutex _mutex;
    std::condition_variable _cv;
    std::set<Entry> _events;
    EventId _lastEventNum = 0;
};


#endif//GUARD