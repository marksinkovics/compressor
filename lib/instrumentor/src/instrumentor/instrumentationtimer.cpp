#include <instrumentor/instrumentationtimer.h>
#include <instrumentor/instrumentor.h>
#include <thread>

InstrumentationTimer::InstrumentationTimer(const std::string& name)
    : m_result({name, 0, 0, 0})
    , m_stopped(false)
{
    m_startTimepoint = std::chrono::steady_clock::now();
}

InstrumentationTimer::~InstrumentationTimer() {
    if(!m_stopped) {
        stop();
    }
}

void InstrumentationTimer::stop() {
    if(m_stopped) {
        return;
    }
    auto endTimepoint = std::chrono::steady_clock::now();
    m_result.start = std::chrono::time_point_cast<std::chrono::microseconds>(m_startTimepoint).time_since_epoch().count();
    m_result.end   = std::chrono::time_point_cast<std::chrono::microseconds>(endTimepoint).time_since_epoch().count();
    m_result.threadID = std::hash<std::thread::id>{}(std::this_thread::get_id());
    Instrumentor::Get().writeProfile(m_result);
    m_stopped = true;
}