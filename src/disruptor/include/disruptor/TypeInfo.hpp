#pragma once

#include <string>
#include <typeinfo>

namespace opencmw::disruptor {

struct TypeInfo {
private:
    const std::type_info *m_typeInfo;
    std::string           m_fullyQualifiedName;
    std::string           m_name;

public:
    explicit TypeInfo(const std::type_info &typeInfo);

    const std::type_info &intrinsicTypeInfo() const;

    const std::string    &fullyQualifiedName() const;
    const std::string    &name() const;

    bool                  operator==(const TypeInfo &rhs) const;

    static std::string    dotNetify(const std::string &typeName);
    static std::string    unqualifyName(const std::string &fullyQualifiedName);
    static std::string    demangleTypeName(const std::string &typeName);
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

namespace std {

template<>
struct hash<opencmw::disruptor::TypeInfo> : public unary_function<opencmw::disruptor::TypeInfo, size_t> {
public:
    size_t operator()(const opencmw::disruptor::TypeInfo &value) const {
        return hash<type_index>()(type_index(value.intrinsicTypeInfo()));
    }
};

} // namespace std
