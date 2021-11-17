#include <URI.hpp>
#include <array>
#include <iostream>

/**
 * simple example to illustrate uri syntax
 */
int main() {
    opencmw::URI<> uri("mdp://User:notSoSecret@localhost.com:20/path/file.ext?queryString#cFrag");
    fmt::print("scheme: '{}' authority: '{}'\n", uri.scheme(), uri.authority());
    fmt::print("user: '{}' password: '{}' hostName: '{}' port: '{}'\n", uri.user(), uri.password(), uri.hostName(), uri.port());
    fmt::print("path: '{}' query: '{}' fragment: '{}'\n", uri.path(), uri.queryParam(), uri.fragment());
    std::cout << "std::ostream URI output: " << uri << '\n';
    fmt::print("fmt::format URI output: {}\n", uri);

    // URI factory examples
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).toString());

    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory().toString());
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").authority("authority").toString());
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").authority("authority").path("path").toString());
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("file").path("path").toString());
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString());

    // query parameter handling
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).queryParam("").addQueryParameter("keyOnly").addQueryParameter("key", "value").toString());
    fmt::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).addQueryParameter("keyOnly").addQueryParameter("key", "value").toString());

    opencmw::URI<> queryURI("http://User:notSoSecretPwd@localhost.com:20/path1/path2/path3/file.ext?k0&k1=v1;k2=v2&k3#cFrag");

    // N.B. map usually defined via const auto map = ...
    const std::unordered_map<std::string, std::optional<std::string>> &map = queryURI.queryParamMap();
    for (const auto &[key, value] : map) {
        // N.B. std::optional<T> are automatically unwrapped int printouts if value is present
        fmt::print("map entry key={} value={}\n", key, value);
    }

    return 0;
}