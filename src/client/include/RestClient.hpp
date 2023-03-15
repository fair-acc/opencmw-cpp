#ifndef OPENCMW_CPP_RESTCLIENT_HPP
#define OPENCMW_CPP_RESTCLIENT_HPP

#ifdef __EMSCRIPTEN__
#include "RestClientEmscripten.hpp"
#else
#include "RestClientNative.hpp"
#endif

#endif // OPENCMW_CPP_RESTCLIENT_HPP
