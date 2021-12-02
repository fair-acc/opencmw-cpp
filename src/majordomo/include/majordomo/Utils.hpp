#ifndef OPENCMW_MAJORDOMO_UTILS_H
#define OPENCMW_MAJORDOMO_UTILS_H

#include <URI.hpp>

namespace opencmw::majordomo {

/**
 * Converts an address URI to the format expected by ZeroMQ, i.e. replace mds:/ and mdp:/ by tcp:/
 */
std::string toZeroMQEndpoint(const opencmw::URI<> &uri) {
    if (uri.scheme() == "mdp" || uri.scheme() == "mds") {
        return opencmw::URI<>::factory(uri).scheme("tcp").toString();
    }

    return uri.str;
}

} // namespace opencmw::majordomo

#endif
