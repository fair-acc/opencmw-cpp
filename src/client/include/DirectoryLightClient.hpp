#ifndef OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP
#define OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP

#include <map>
#include <ranges>
#include <string_view>
#include <zmq/ZmqUtils.hpp>

using namespace std::chrono_literals;

auto parse = [](const std::string &reply) {
    auto urlDecode = [](std::string str) {
        std::string ret;
        ret.reserve(str.length());
        char        ch;
        std::size_t len = str.length();
        for (std::size_t i = 0; i < len; i++) {
            if (str[i] != '%') {
                if (str[i] == '+') {
                    ret += ' ';
                } else {
                    ret += str[i];
                }
            } else if (i + 2 < len) {
                auto toHex = [](char c) {
                    if (c >= '0' && c <= '9') return c - '0';
                    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                    throw std::runtime_error("Invalid hexadecimal number");
                };
                ch = static_cast<char>('\x10' * toHex(str.at(i + 1)) + toHex(str.at(i + 2)));
                ret += ch;
                i = i + 2;
            }
        }
        return ret;
    };
    using std::operator""sv;
    std::map<std::string, std::map<std::string, std::map<std::string, std::variant<std::string, int, long>>>> devices;
    if (reply.starts_with("ERROR")) {
        throw std::runtime_error("Nameserver returned an error");
    }
    // each line: one device
    // auto lines = reply | std::views::lazy_split("\n"sv);
    std::ranges::split_view lines{ reply, "\n"sv };
    // auto tokens = lines | std::views::transform([](auto &l) {return std::views::split(" "sv);});
    auto split_lines = std::views::transform([](auto str) { return std::ranges::split_view{ str, " "sv }; });
    for (auto l : lines | split_lines) {
        if (l.empty()) {
            continue;
        }
        std::string devicename{ std::string_view{ l.front().data(), l.front().size() } };
        auto        l2 = std::views::drop(l, 1);
        if (l2.empty()) {
            devices.insert({ devicename, {} });
            continue;
        }
        std::string classname{ std::string_view{ l2.front().data(), l2.front().size() } };
        if (classname.starts_with("*NOT_BOUND*") || classname.starts_with("*UNKNOWN*")) {
            devices.insert({ devicename, {} });
            continue;
        }
        auto l3 = std::views::drop(l2, 1);
        if (l3.empty()) {
            devices.insert({ devicename, {} });
            continue;
        }
        std::map<std::string, std::map<std::string, std::variant<std::string, int, long>>> attributes{};
        for (auto attributeString : l3) {
            auto tokens = std::views::split(attributeString, "#"sv);
            if (tokens.empty()) {
                continue;
            }
            std::string addresfieldcount = { tokens.front().data(), tokens.front().size() };
            auto        seperatorPos     = addresfieldcount.find("://");
            std::string proto            = addresfieldcount.substr(0, seperatorPos + 3);
            std::size_t i;
            char       *end        = to_address(addresfieldcount.end());
            std::size_t fieldCount = std::strtoull(addresfieldcount.data() + seperatorPos + 3, &end, 10);
            auto [map, _]          = attributes.insert({ proto, {} });
            map->second.insert({ "Classname", classname });

            auto        range    = std::views::drop(tokens, 1);
            auto        iterator = range.begin();
            std::size_t n        = 0;
            while (n < fieldCount) {
                std::string_view fieldNameView{ &(*iterator).front(), (*iterator).size() };
                std::string      fieldname{ fieldNameView.substr(0, fieldNameView.size() - 1) };
                iterator++;
                std::string type{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                if (type == "string") {
                    iterator++;
                    std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                    auto        parsed = std::to_address(sizeString.end());
                    std::size_t size   = std::strtoull(sizeString.data(), &parsed, 10);
                    iterator++;
                    std::string string{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                    map->second.insert({ fieldname, urlDecode(string) });
                } else if (type == "int") {
                    iterator++;
                    std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                    int         number = std::atoi(sizeString.data());
                    map->second.insert({ fieldname, number });
                } else if (type == "long") {
                    iterator++;
                    std::string sizeString{ std::string_view{ &(*iterator).front(), (*iterator).size() } };
                    auto        parsed = std::to_address(sizeString.end());
                    long        number = std::strtol(sizeString.data(), &parsed, 10);
                    map->second.insert({ fieldname, number });
                } else {
                    FAIL(fmt::format("unknown type: {}, field: {}, tokens: {}", type, fieldname, tokens));
                }
                iterator++;
                n++;
            }
        }
        devices.insert({ devicename, attributes });
    }
    return devices;
};

std::string resolveDirectoryLight(std::vector<std::string> devices, std::string_view nameserver, opencmw::zmq::Context &ctx, std::chrono::milliseconds timeout = 500ms) {
    const opencmw::zmq::Socket socket{ ctx, ZMQ_STREAM };
    if (!opencmw::zmq::invoke(zmq_connect, socket, nameserver).isValid()) {
        throw std::runtime_error("could not connect to nameserver.");
    }
    std::string id;
    std::size_t data_len = 255;
    id.resize(data_len);
    if (!opencmw::zmq::invoke(zmq_getsockopt, socket, ZMQ_IDENTITY, id.data(), &data_len).isValid()) {
        throw std::runtime_error("could not get socket identity");
    }
    id.resize(data_len);

    const std::string          query = fmt::format("get-device-info\n@client-info opencmw-cpp-directory-light-client\n@version 0.0.1\n{}\n\n", fmt::join(devices, "\n"));

    opencmw::zmq::MessageFrame identityFrame{ std::string{ id } };
    if (!identityFrame.send(socket, ZMQ_SNDMORE).isValid()) {
        throw std::runtime_error("error sending socket id");
    }
    opencmw::zmq::MessageFrame queryFrame{ std::string{ query } };
    if (!queryFrame.send(socket, 0).isValid()) {
        throw std::runtime_error("error sending query frame");
    }

    auto        start_time = std::chrono::system_clock::now();
    std::string result;
    bool        data_received = false;
    while ((result.empty() || (data_received && !result.empty())) && std::chrono::system_clock::now() - start_time < timeout) { // wait for a maximum of 5 seconds
        data_received = false;
        opencmw::zmq::MessageFrame idFrame;
        const auto                 byteCountResultId = idFrame.receive(socket, ZMQ_DONTWAIT);
        if (!byteCountResultId.isValid() || byteCountResultId.value() < 1) {
            continue;
        }
        if (idFrame.data() != id) {
            throw std::runtime_error("connection identifier from socket does not match connection");
        }
        opencmw::zmq::MessageFrame frame;
        for (auto byteCountResult = frame.receive(socket, ZMQ_DONTWAIT); byteCountResult.value() < 0; byteCountResult = frame.receive(socket, ZMQ_DONTWAIT)) {
        }
        if (frame.size() > 0) {
            result += frame.data();
            data_received = true;
        }
    }
    if (!opencmw::zmq::invoke(zmq_disconnect, socket, nameserver).isValid()) {
        throw std::runtime_error("could not disconnect");
    }
    return result;
}
#endif // OPENCMW_CPP_DIRECTORYLIGHTCLIENT_HPP
