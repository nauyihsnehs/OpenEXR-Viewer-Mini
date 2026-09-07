#include "StdIStream.h"
#include <limits>
#include <stdexcept>

StdIStream::StdIStream(std::istream& stream): IStream("Stream")
{
    m_stream << stream.rdbuf();
    if (stream.bad() || m_stream.bad())
        throw std::runtime_error("Cannot copy EXR stream.");
    m_stream.seekg(0);
}

bool StdIStream::read(char c[], int n)
{
    if (n < 0 || !m_stream.read(c, n))
        throw std::runtime_error("Unexpected end of EXR stream or read error.");
    return m_stream.rdbuf()->sgetc() != std::char_traits<char>::eof();
}

uint64_t StdIStream::tellg()
{
    const auto pos = m_stream.tellg();
    if (pos < 0) throw std::runtime_error("Cannot query EXR stream position.");
    return static_cast<uint64_t>(pos);
}

void StdIStream::seekg(uint64_t pos)
{
    if (pos > uint64_t(std::numeric_limits<std::streamoff>::max()))
        throw std::runtime_error("Invalid EXR stream position.");
    m_stream.clear();
    if (!m_stream.seekg(static_cast<std::streamoff>(pos)))
        throw std::runtime_error("Cannot seek in EXR stream.");
}
