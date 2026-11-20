// A small JSON reader for the catalogs and param_help.json (no JUCE, no
// downloaded dependency). It reads the whole of RFC 8259; numbers are kept as
// doubles, objects keep their members in file order (a JS object's key order
// for the keys the catalogs use), strings are UTF-8.
//
//   lexcat::Json j = lexcat::Json::parse(text);   // throws lexcat::JsonError
//   j["programs"][0]["name"].string()
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lexcat {

struct JsonError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;

    static Json parse(std::string_view text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool boolean() const { expect(Type::Bool, "a boolean"); return bool_; }
    double number() const { expect(Type::Number, "a number"); return number_; }
    int integer() const;   // a number that is a whole int
    const std::string &string() const { expect(Type::String, "a string"); return string_; }
    const std::vector<Json> &array() const { expect(Type::Array, "an array"); return items_; }
    const std::vector<std::pair<std::string, Json>> &object() const { expect(Type::Object, "an object"); return members_; }

    size_t size() const;
    const Json &operator[](size_t i) const;
    // A member, or a null Json if the object has none (or this is not an object).
    const Json &operator[](std::string_view key) const;
    bool has(std::string_view key) const;

private:
    void expect(Type t, const char *what) const {
        if (type_ != t) {
            throw JsonError(std::string("JSON: expected ") + what);
        }
    }

    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0;
    std::string string_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> members_;

    friend class JsonReader;
};

}  // namespace lexcat
