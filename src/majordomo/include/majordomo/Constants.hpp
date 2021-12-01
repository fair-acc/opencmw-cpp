#ifndef OPENCMW_MAJORDOMO_CONSTANTS_H
#define OPENCMW_MAJORDOMO_CONSTANTS_H

#include <string>

namespace opencmw::majordomo {
// TODO: Make constexpr as std::string is not yet constexpr
/*constexpr*/ const std::string SCHEME_TCP                 = "tcp";
/*constexpr*/ const std::string SCHEME_MDP                 = "mdp";
/*constexpr*/ const std::string SCHEME_MDS                 = "mds";
/*constexpr*/ const std::string SCHEME_INPROC              = "inproc";
/*constexpr*/ const std::string SUFFIX_ROUTER              = "/router";
/*constexpr*/ const std::string SUFFIX_PUBLISHER           = "/publisher";
/*constexpr*/ const std::string SUFFIX_SUBSCRIBE           = "/subscribe";
/*constexpr*/ const std::string INPROC_BROKER              = "inproc://broker";
/*constexpr*/ const std::string INTERNAL_ADDRESS_BROKER    = INPROC_BROKER + SUFFIX_ROUTER;
/*constexpr*/ const std::string INTERNAL_ADDRESS_PUBLISHER = INPROC_BROKER + SUFFIX_PUBLISHER;
/*constexpr*/ const std::string INTERNAL_ADDRESS_SUBSCRIBE = INPROC_BROKER + SUFFIX_SUBSCRIBE;
/*constexpr*/ const std::string INTERNAL_SERVICE_NAMES     = "mmi.service";
} // namespace opencmw::majordomo

#endif
