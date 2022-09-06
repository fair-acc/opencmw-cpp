#ifndef OPENCMW_MUSTACHESERIALISER_H
#define OPENCMW_MUSTACHESERIALISER_H

#include <mustache.hpp>

#include <IoSerialiserJson.hpp>
#include <MIME.hpp>
#include <opencmw.hpp>
#include <TimingCtx.hpp>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(assets);

struct FieldMetadata {
    FieldMetadata(auto _name, auto _type, auto _unit, auto _description, auto _value)
        : name(std::move(_name)), type(std::move(_type)), unit(std::move(_unit)), description(std::move(_description)), value(std::move(_value)) {
    }

    std::string name;
    std::string type;
    std::string unit;
    std::string description;
    std::string value;
};
ENABLE_REFLECTION_FOR(FieldMetadata, name, type, unit, description, value)

struct ObjectMetadata {
    std::vector<FieldMetadata> fields;
    std::string                typeName;
};
ENABLE_REFLECTION_FOR(ObjectMetadata, fields, typeName)

namespace opencmw::mustache {

template<typename T>
class mustache_data;

template<typename T>
using mustache = kainjow::mustache_ns<std::string, mustache_data<T>>;

// Type erase the data
class mustache_data_base {
protected:
    enum class type {
        string,
        bool_true,
        bool_false,
        list_empty,
        list_non_empty,
        multi_array,
        object
    };
    type _type;

    explicit mustache_data_base(type dataType)
        : _type(dataType) {}

    mustache_data_base(const mustache_data_base &) = delete;
    mustache_data_base &operator=(const mustache_data_base &) = delete;
    mustache_data_base(mustache_data_base &&)                 = delete;
    mustache_data_base &operator=(mustache_data_base &&) = delete;

public:
    virtual ~mustache_data_base() = default;

    virtual const mustache_data_base *get([[maybe_unused]] const std::string &name) const {
        return nullptr;
    }

    virtual const std::string &string_value() const {
        static std::string defaultString;
        return defaultString;
    }

    virtual const mustache_data_base *next_list_item() const {
        return nullptr;
    }

    bool is_string() const {
        return _type == type::string;
    }

    bool is_false() const {
        return _type == type::bool_false;
    }

    bool is_true() const {
        return _type == type::bool_true;
    }

    bool is_empty_list() const {
        return _type == type::list_empty;
    }

    bool is_non_empty_list() const {
        return _type == type::list_non_empty;
    }

    bool is_partial() const {
        return false;
    }

    const mustache_data_base &partial_value() const {
        throw std::runtime_error("We don't support partial values in mustache");
    }

    const std::string operator()() const {
        throw std::runtime_error("We don't support partial values in mustache");
    }
};

template<typename Rep, units::Quantity Q, const basic_fixed_string description, const ExternalModifier modifier, const basic_fixed_string... groups>
class mustache_data<Annotated<Rep, Q, description, modifier, groups...>> : public mustache_data<Rep> {
public:
    mustache_data(const auto &value)
        : mustache_data<Rep>(value.value()) {}
};

template<>
class mustache_data<std::string> : public mustache_data_base {
private:
    std::string _value;

public:
    explicit mustache_data(std::string value)
        : mustache_data_base(type::string)
        , _value(std::move(value)) {}
    [[nodiscard]] const std::string &string_value() const override {
        return _value;
    }
    [[nodiscard]] const mustache_data_base *get(const std::string & /*name*/) const override {
        // We don't have name as we are a simple string, not a structure, returning ourselves
        return this;
    }
};

template<typename Type, std::size_t Dims>
class mustache_data<MultiArray<Type, Dims>> : public mustache_data<std::string> {
    using multi_array_type = MultiArray<Type, Dims>;
public:
    explicit mustache_data(multi_array_type val) : mustache_data<std::string>(std::to_string(val.dimensions()[0])) { }
        // : mustache_data<std::string>([&val]() {
        //     IoBuffer buffer;
        //     return IoSerialiser<Json, MultiArray<Type, Dims>>::serialise(buffer, val);
        //     return std::string(buffer.template asString());
        // }()) {}

    // [[nodiscard]] const mustache_data_base *get(const std::string & /*name*/) const override {
    //     // We don't have name as we are a simple string, not a structure, returning ourselves
    //     return nullptr;
    // }
};

template<Number T>
class mustache_data<T> : public mustache_data<std::string> {
public:
    explicit mustache_data(T val)
        : mustache_data<std::string>(std::to_string(val)) {}
};

template<>
class mustache_data<std::string_view> : public mustache_data<std::string> {
public:
    explicit mustache_data(std::string_view val)
        : mustache_data<std::string>(std::string(val)) {}
};

template<>
class mustache_data<opencmw::MIME::MimeType> : public mustache_data<std::string_view> {
public:
    template<typename T>
    explicit mustache_data(T val)
        : mustache_data<std::string_view>(val.typeName()) {}
};

template<>
class mustache_data<opencmw::TimingCtx> : public mustache_data<std::string> {
public:
    template<typename T>
    explicit mustache_data(T val)
        : mustache_data<std::string>(val.toString()) {}
};

template<typename T>
requires units::is_derived_from_specialization_of<T, std::vector>
class mustache_data<T> : public mustache_data_base {
private:
    const T &_value;

    // Allowing single iteration over the list
    mutable typename T::const_iterator          _it;
    mutable std::unique_ptr<mustache_data_base> _current;

public:
    explicit mustache_data(const T &value)
        : mustache_data_base(value.empty() ? type::list_empty : type::list_non_empty)
        , _value(std::move(value))
        , _it(_value.cbegin()) {}

    const mustache_data_base *next_list_item() const {
        _current.reset();

        if (_it == _value.cend()) return nullptr;

        _current = std::make_unique<mustache_data<typename T::value_type>>(*_it);
        ++_it;

        return _current.get();
    }
};

template<>
class mustache_data<std::unordered_map<std::string, std::unique_ptr<mustache_data_base>>> : public mustache_data_base {
private:
    std::unordered_map<std::string, std::unique_ptr<mustache_data_base>> _value;

public:
    explicit mustache_data(auto &&value)
        : mustache_data_base(type::object)
        , _value(std::forward<decltype(value)>(value)) {}

    const mustache_data_base *get(const std::string &name) const override {
        auto it = _value.find(name);
        return it != _value.cend() ? it->second.get() : nullptr;
    }
};

template<typename T>
requires(refl::trait::is_reflectable_v<T> && (not Number<T>) ) class mustache_data<T> : public mustache_data_base {
private:
    const T &_value;
    // These are used as unique pointers with type erased to destroy the
    // allocated children nodes
    mutable std::vector<std::shared_ptr<void>> _children;

public:
    mustache_data(const T &value)
        : mustache_data_base(
                [&value] {
                    (void) value;
                    if constexpr (std::is_same_v<T, std::string>) {
                        return type::string;
                    } else if constexpr (std::is_same_v<T, bool>) {
                        return value ? type::bool_true : type::bool_false;
                    } else if constexpr (units::is_derived_from_specialization_of<T, std::vector>) {
                        return value.empty() ? type::list_empty : type::list_non_empty;
                    }
                    return type::object;
                }())
        , _value(value) {}

    ~mustache_data() override = default;

    const mustache_data_base *get(const std::string &name) const override {
        // refl::util::find_one wants a constexpr lambda, which this one is not
        mustache_data_base *result = nullptr;

        for_each(refl::reflect(_value).members,
                [this, &result, &name]<typename Member>(const Member member) {
                    if constexpr (is_field(member) && !is_static(member)) {
                        std::string_view member_name(member.name.c_str(), member.name.size);
                        if (name == member_name) {
                            if (!result) {
                                auto new_child = std::make_shared<mustache_data<typename Member::value_type>>(member(_value));
                                result         = new_child.get();
                                _children.emplace_back(std::move(new_child));
                            }
                        }
                    }
                });

        return result;
    }

    const std::string &string_value() const override {
        if constexpr (std::is_same_v<T, std::string>) {
            return _value;
        } else {
            static std::string defaultString;
            return defaultString;
        }
    }
};

template<typename T, typename TVal = std::remove_cvref_t<T>>
auto serialiseWithFieldMetadata(T &&object) {
    static_assert(ReflectableClass<TVal>);
    ObjectMetadata meta;

    for_each(refl::reflect(object).members,
            [&meta, &object]<typename Member>(const Member member) {
                if constexpr (is_field(member) && !is_static(member)) {
                    using MemberType              = std::remove_reference_t<typename Member::value_type>;
                    constexpr bool isAnnotated    = is_annotated<MemberType>;

                    auto           getStringValue = [&]<typename ObjectMemberType>(const ObjectMemberType &objectMemberValue) -> std::string {
                        if constexpr (std::is_same_v<ObjectMemberType, std::string>) {
                            return objectMemberValue;
                        } else if constexpr (std::is_same_v<ObjectMemberType, std::string_view>) {
                            return std::string(objectMemberValue);
                        } else if constexpr (std::is_same_v<ObjectMemberType, MIME::MimeType>) {
                            return std::string(objectMemberValue.typeName());
                        } else if constexpr (std::is_same_v<ObjectMemberType, opencmw::TimingCtx>) {
                            return std::string(objectMemberValue.toString());
                        } else {
                            opencmw::IoBuffer buffer;
                            if constexpr (ReflectableClass<ObjectMemberType>) {
                                // IoSerialiser<opencmw::Json, ObjectMemberType>::serialise(buffer, FieldDescriptionShort{ .fieldName = member.name.c_str() }, objectMemberValue);
                                opencmw::serialise<opencmw::Json>(buffer, objectMemberValue);
                            } else {
                                IoSerialiser<opencmw::Json, ObjectMemberType>::serialise(buffer, FieldDescriptionShort{ .fieldName = member.name.c_str() }, objectMemberValue);
                            }
                            return std::string(buffer.asString());
                        }
                    };

                    meta.typeName = refl::reflect<TVal>().name.data;
                    if constexpr (isAnnotated) {
                        const auto      &objectValue = member(object);
                        std::string_view name(member.name.data);
                        std::string_view type(typeName<typename MemberType::rep>);
                        std::string_view unit        = objectValue.getUnit();
                        std::string_view description = objectValue.getDescription();
                        auto             value       = getStringValue(objectValue.value());
                        meta.fields.emplace_back(name, type, unit, description, std::move(value));
                    } else {
                        const auto      &objectValue = member(object);
                        std::string_view name(member.name.data);
                        std::string_view type(typeName<MemberType>);
                        std::string_view unit;
                        std::string_view description;
                        auto             value       = getStringValue(objectValue);
                        meta.fields.emplace_back(name, type, unit, description, std::move(value));
                    }
                }
            });

    return meta;
}

template<typename Stream, typename... Objects>
void serialise(const std::string &workerName, Stream &out, std::pair<std::string, const Objects &> &&...namedObjects) {
    static_assert((ReflectableClass<std::remove_cvref_t<Objects>> && ...));

    static const auto fileName = "assets/mustache/" + workerName + ".mustache";
    const auto        fs       = cmrc::assets::get_filesystem();

    using mustache_ns          = kainjow::mustache_ns<std::string, mustache_data_base>;
    // Structured bindings here break the linker...
    // Should be const auto [ customTemplateExists, mustache ]
    static auto pair = [&fs] {
        const bool  customTemplateExists = fs.exists(fileName.data());
        const auto  file                 = fs.open(customTemplateExists ? fileName.data() : "assets/mustache/default.mustache");
        std::string contents(file.cbegin(), file.cend());
        return std::make_pair(customTemplateExists, mustache_ns::mustache(contents));
    }();

    const bool customTemplateExists = pair.first;
    auto      &mustache             = pair.second;

    // Map of objects that we want mustache to see
    std::unordered_map<std::string, std::unique_ptr<mustache_data_base>> value;

    if (customTemplateExists) {
        // Each obejct is a pair { name, object }
        auto collectObject = [&](auto &&namedObject) {
            auto &name   = namedObject.first;
            auto &object = namedObject.second;
            using Object = std::remove_cvref_t<decltype(object)>;
            value.emplace(std::move(name), new mustache_data<Object>(std::move(object)));
        };

        (collectObject(std::move(namedObjects)), ...);

        mustache_data<decltype(value)> data(std::move(value));
        mustache.render(data, out);

    } else {
        // Map of objects that we want mustache to see
        std::unordered_map<std::string, ObjectMetadata> meta;

        // Each object is a pair { name, object }
        auto collectObject = [&](auto &&namedObject) {
            auto &name   = namedObject.first;
            auto &object = namedObject.second;
            meta.emplace(std::move(name), serialiseWithFieldMetadata(std::forward<decltype(object)>(object)));
        };
        (collectObject(std::move(namedObjects)), ...);

        // Now that we collected all the object meta-data, we can create
        // the data facade for mustache
        for (const auto &[key, metaObject] : meta) {
            value.emplace(key, std::make_unique<mustache_data<ObjectMetadata>>(metaObject));
        }

        mustache_data<decltype(value)> data(std::move(value));
        mustache.render(data, out);
    }
}

} // namespace opencmw::mustache

#endif // OPENCMW_MUSTACHESERIALISER_H
