#include "events.h"
using namespace std;

#ifndef EMU
inline void Z_EventNotify(EventAction, const EventQueue::Entry*){}
#endif

void EventQueue::PlanEvent(Event event, Duration duration, bool deletePrevious)
{
    PlanEvent(event, Clock::now() + duration, deletePrevious);
}

void EventQueue::PlanEvent(Event event, Time time, bool deletePrevious)
{
    {
        lock_guard<mutex> lock(_mutex);
        Z_EventNotify(EA_New, nullptr);
        if (deletePrevious)
        {
            EraseFromQueueNotSync(event.Type());
        }
        auto it = _events.emplace(event, time, ++_lastEventNum).first;
        Z_EventNotify(EA_Plan, &*it);
    }
    _cv.notify_all();
}

Event EventQueue::WaitEvent()
{
    unique_lock<mutex> lock(_mutex);
    Z_EventNotify(EA_Wait, nullptr);
    while (_events.empty() || _events.begin()->time > Clock::now())
    {
        if (_events.empty())
            _cv.wait(lock);
        else
            _cv.wait_until(lock, _events.begin()->time);
    }
    Z_EventNotify(EA_Dispatch, &*_events.begin());
    Event result = _events.begin()->event;
    _events.erase(_events.begin());
    return result;
}

void EventQueue::DeleteEvents(EventType type)
{
    {
        lock_guard<mutex> lock(_mutex);
        Z_EventNotify(EA_New, nullptr);
        EraseFromQueueNotSync(type);
    }
    _cv.notify_all();
}

void EventQueue::EraseFromQueueNotSync(EventType type)
{
    for (auto it = _events.begin(); it != _events.end();)
    {
        if (it->event.Type() == type)
        {
            Z_EventNotify(EA_Delete, &*it);
            it = _events.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
