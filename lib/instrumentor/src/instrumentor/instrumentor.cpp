#include <instrumentor/instrumentor.h>

#include <thread>

Instrumentor::Instrumentor() = default;

Instrumentor::~Instrumentor()
{
    endSession();
}

void Instrumentor::beginSession(const std::string& name, const std::string& filepath)
{
    if (m_activeSession)
    {
        endSession();
    }
    m_activeSession = true;
    m_outputStream.open(filepath);
    writeHeader();
    m_sessionName = name;
}

void Instrumentor::endSession()
{
    if (!m_activeSession)
    {
        return;
    }
    m_activeSession = false;
    writeFooter();
    m_outputStream.close();
    m_profileCount = 0;
}

void Instrumentor::writeProfile(const ProfileResult& result)
{
    std::scoped_lock lock(m_lock);

    if (m_profileCount++ > 0)
    {
        m_outputStream << ",";
    }

    std::string name = result.name;
    std::replace(name.begin(), name.end(), '"', '\'');

    m_outputStream << "{";
    m_outputStream << R"("cat":"function",)";
    m_outputStream << "\"dur\":" << (result.end - result.start) << ',';
    m_outputStream << R"("name":")" << name << "\",";
    m_outputStream << R"("ph":"X",)";
    m_outputStream << "\"pid\":0,";
    m_outputStream << "\"tid\":" << result.threadID << ",";
    m_outputStream << "\"ts\":" << result.start;
    m_outputStream << "}";
    m_outputStream.flush();
}

void Instrumentor::writeHeader()
{
    m_outputStream << R"({"otherData": {},"traceEvents":[)";
    m_outputStream.flush();
}

void Instrumentor::writeFooter()
{
    m_outputStream << "]}";
    m_outputStream.flush();
}
