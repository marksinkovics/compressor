//
// This instrumentation module was based on
// https://gist.github.com/TheCherno/31f135eea6ee729ab5f26a6908eb3a5e
//

#ifndef Instrumentor_h
#define Instrumentor_h

#include <string>
#include <algorithm>
#include <fstream>
#include <mutex>

#include <instrumentor/profileresult.h>
#include <instrumentor/instrumentationtimer.h>

#if PROFILING == 1
    #define PROFILE_SESSION(name) Instrumentor::Get().beginSession(name);
    #define PROFILE_SCOPE(name) InstrumentationTimer timer##__LINE__(name);
    #define PROFILE_FUNCTION() PROFILE_SCOPE(__PRETTY_FUNCTION__)
    #define PROFILE_START(var_name, name) InstrumentationTimer var_name(name);
    #define PROFILE_END(var_name, name) var_name.stop();
#else
    #define PROFILE_SESSION(name)
    #define PROFILE_SCOPE(name)
	#define PROFILE_FUNCTION()
    #define PROFILE_START(var_name, name)
    #define PROFILE_END(var_name, name)
#endif


class Instrumentor {
private:
    std::string m_sessionName = "None";
    std::ofstream m_outputStream;
    int m_profileCount = 0;
    std::mutex m_lock;
    bool m_activeSession = false;

    Instrumentor();
public:
    static Instrumentor& Get() {
        static Instrumentor instance;
        return instance;
    }

    ~Instrumentor();
    void beginSession(const std::string& name, const std::string& filepath = "result.json");
    void endSession();
    void writeProfile(const ProfileResult& result);
	void writeHeader();
	void writeFooter();
};

#endif /* Instrumentor_h */
