#pragma once

#include <regex>
#include <string>
#include <typeinfo>
#include <vector>

#if defined(__GNUC__)
#include <cxxabi.h>
#endif /* __GNUC__ */

namespace opencmw::disruptor {

struct TypeInfo {
private:
    const std::type_info *_typeInfo;
    std::string           _fullyQualifiedName;
    std::string           _name;

public:
    explicit TypeInfo(const std::type_info &typeInfo)
        : _typeInfo(&typeInfo)
        , _fullyQualifiedName(dotNetify(demangleTypeName(_typeInfo->name())))
        , _name(unqualifyName(_fullyQualifiedName)) {
    }

    const std::type_info &intrinsicTypeInfo() const { return *_typeInfo; }
    const std::string    &fullyQualifiedName() const { return _fullyQualifiedName; }
    const std::string    &name() const { return _name; }

    bool                  operator==(const TypeInfo &rhs) const { return intrinsicTypeInfo() == rhs.intrinsicTypeInfo(); }

    static std::string    dotNetify(const std::string &typeName) {
        std::regex pattern("::");
        return std::regex_replace(typeName, pattern, ".");
    }
    static std::string unqualifyName(const std::string &fullyQualifiedName) {
        auto position = fullyQualifiedName.rfind('.');
        if (position == std::string::npos) {
            return {};
        } else {
            return fullyQualifiedName.substr(position);
        }
    }
    static std::string demangleTypeName(const std::string &typeName) {
#if defined(__GNUC__)
        int  status;

        auto demangledName = abi::__cxa_demangle(typeName.c_str(), 0, 0, &status);
        if (demangledName == nullptr)
            return typeName;

        std::string result = demangledName;
        free(demangledName);
        return result;
#else
        std::string demangled = typeName;
        demangled             = std::regex_replace(demangled, std::regex("(const\\s+|\\s+const)"), std::string());
        demangled             = std::regex_replace(demangled, std::regex("(volatile\\s+|\\s+volatile)"), std::string());
        demangled             = std::regex_replace(demangled, std::regex("(static\\s+|\\s+static)"), std::string());
        demangled             = std::regex_replace(demangled, std::regex("(class\\s+|\\s+class)"), std::string());
        demangled             = std::regex_replace(demangled, std::regex("(struct\\s+|\\s+struct)"), std::string());
        return demangled;
#endif /* defined(__GNUC__) */
    }
};

namespace Utils {

template<typename T>
const TypeInfo &getMetaTypeInfo() {
    static TypeInfo result(typeid(T));
    return result;
}

} // namespace Utils
} // namespace opencmw::disruptor

#include <functional>
#include <typeindex>

template<>
struct std::hash<opencmw::disruptor::TypeInfo> {
public:
    size_t operator()(const opencmw::disruptor::TypeInfo &value) const {
        return hash<type_index>()(type_index(value.intrinsicTypeInfo()));
    }
};
