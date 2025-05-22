#include <URI.hpp>
#include <array>
#include <iostream>
#include <print>
#include <Formatter.hpp>
#include <format>

/**
 * simple example to illustrate uri syntax
 */
int main() {
    opencmw::URI<> uri("mdp://User:notSoSecret@localhost.com:20/path/file.ext?queryString#cFrag");
    std::print("scheme: '{}' authority: '{}'\n", uri.scheme(), uri.authority());
    std::print("user: '{}' password: '{}' hostName: '{}' port: '{}'\n", uri.user(), uri.password(), uri.hostName(), uri.port());
    std::print("path: '{}' query: '{}' fragment: '{}'\n", uri.path(), uri.queryParam(), uri.fragment());
    std::cout << "std::ostream URI output: " << uri << '\n';
    std::print("std::format URI output: {}\n", uri);

    // URI factory examples
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).toString());

    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory().toString());
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").authority("authority").toString());
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").authority("authority").path("path").toString());
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("file").path("path").toString());
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory().scheme("mdp").hostName("localhost").port(8080).path("path").queryParam("key=value").fragment("fragment").toString());

    // query parameter handling
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).queryParam("").addQueryParameter("keyOnly").addQueryParameter("key", "value").toString());
    std::print("new URI: \"{}\"\n", opencmw::URI<>::factory(uri).addQueryParameter("keyOnly").addQueryParameter("key", "value").toString());

    opencmw::URI<> queryURI("http://User:notSoSecretPwd@localhost.com:20/path1/path2/path3/file.ext?k0&k1=v1;k2=v2&k3#cFrag");

    // N.B. map usually defined via const auto map = ...
    const std::unordered_map<std::string, std::optional<std::string>> &map = queryURI.queryParamMap();
    for (const auto &[key, value] : map) {
        // N.B. std::optional<T> are automatically unwrapped int printouts if value is present
        std::print("map entry key={} value={}\n", key, value);
    }

    return 0;
}