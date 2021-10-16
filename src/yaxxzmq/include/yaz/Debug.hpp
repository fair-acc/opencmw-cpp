
#ifndef YAZ_DEBUG_H
#define YAZ_DEBUG_H

#include <iostream>

// TODO: Make a proper debug function
inline auto &debug() {
    return std::cerr;
}

#endif // include guard

