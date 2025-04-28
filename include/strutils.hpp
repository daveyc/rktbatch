#pragma once

#include <string>
#include <cstring>

namespace strutils
{
    inline char* dupstr(const char *old) {
        char *s = strdup(old);
        if (!s) throw std::bad_alloc();
        return s;
    }

    inline char* dupstr(const std::string &old) {
        return dupstr(old.c_str());
    }

    inline void ltrim(std::string &s, const std::string &delims = " \t\n") {
        s.erase(0, s.find_first_not_of(delims));
    }

    inline bool starts_with(const std::string &s, const std::string &prefix) {
        return s.rfind(prefix, 0) == 0;
    }
}