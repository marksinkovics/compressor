#ifndef InstrumentationTimer_h
#define InstrumentationTimer_h

#include <chrono>
#include <instrumentor/profileresult.h>

class InstrumentationTimer {
private:
	ProfileResult m_result;
	std::chrono::time_point<std::chrono::steady_clock> m_startTimepoint;
	bool m_stopped;
public:
	InstrumentationTimer(const std::string& name);
	~InstrumentationTimer();
	void stop();
};

#endif /* InstrumentationTimer_h */