#ifndef OPENCMW_MAJORDOMO_CONSTANTS_H
#define OPENCMW_MAJORDOMO_CONSTANTS_H

#include <string>

#include <URI.hpp>

namespace opencmw::majordomo {
// TODO: Make constexpr as std::string is not yet constexpr
/*constexpr*/ const std::string SCHEME_TCP                 = "tcp";
/*constexpr*/ const std::string SCHEME_MDP                 = "mdp";
/*constexpr*/ const std::string SCHEME_MDS                 = "mds";
/*constexpr*/ const std::string SCHEME_INPROC              = "inproc";
/*constexpr*/ const std::string SUFFIX_ROUTER              = "router";
/*constexpr*/ const std::string SUFFIX_PUBLISHER           = "publisher";
/*constexpr*/ const std::string SUFFIX_SUBSCRIBE           = "subscribe";
const opencmw::URI<>            INPROC_BROKER              = opencmw::URI<>("inproc://broker");
const opencmw::URI<>            INTERNAL_ADDRESS_BROKER    = opencmw::URI<>::factory(INPROC_BROKER).path(SUFFIX_ROUTER).build();
const opencmw::URI<>            INTERNAL_ADDRESS_PUBLISHER = opencmw::URI<>::factory(INPROC_BROKER).path(SUFFIX_PUBLISHER).build();
const opencmw::URI<>            INTERNAL_ADDRESS_SUBSCRIBE = opencmw::URI<>::factory(INPROC_BROKER).path(SUFFIX_SUBSCRIBE).build();
/*constexpr*/ const std::string INTERNAL_SERVICE_NAMES     = "/mmi.service";
const opencmw::URI<>            INTERNAL_SERVICE_NAMES_URI = opencmw::URI<>("/mmi.service");
} // namespace opencmw::majordomo

#endif
