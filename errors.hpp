#pragma once 

#include <stdexcept>
#include <cerrno>

inline void throwError(const std::string & msg)
{
    throw std::runtime_error( std::string( msg + ": " + strerror( errno ) ) );
}
