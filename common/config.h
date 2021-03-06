#pragma once

#include "rapidjson/rapidjson.h"
#include "rapidjson/error/en.h"
#include "rapidjson/document.h"

#define __CL_ENABLE_EXCEPTIONS
#include "cl.hpp"

#include <string>
#include <vector>
#include <stdexcept>
#include <map>
#include <memory>

/// This file contains a set of classes which serve to form a layer over the
/// rapidjson parser, which can easily be used to validate and read json
/// fields into specific types.
/// To validate a specific type, just add a specialization of JsonGetter<>
/// for the type that you want to validate.

/// Different components of the output impulse.
enum OutputMode { ALL, IMAGE_ONLY, DIFFUSE_ONLY };

/// A simple interface for a JsonValidator.
struct JsonValidatorBase {
    /// Overload to dictate what happens when a JsonValidator is Run on a Value
    virtual void run(const rapidjson::Value & value) const = 0;
};

struct OptionalValidator;
struct RequiredValidator;

template <typename T>
struct ValueJsonValidator;
template <typename T, typename U>
struct FieldJsonValidator;

/// Used to register required and optional fields that should be present in a
/// config file.
/// Also has the ability to parse a value for these required and optional
/// fields.
/// You almost definitely want an instance of THIS CLASS rather than any other.
class ConfigValidator : public JsonValidatorBase {
public:
    template <typename T>
    void addOptionalValidator(const std::string & s, T & t) {
        validators.emplace_back(
            new FieldJsonValidator<T, OptionalValidator>(s, t));
    }

    template <typename T>
    void addRequiredValidator(const std::string & s, T & t) {
        validators.emplace_back(
            new FieldJsonValidator<T, RequiredValidator>(s, t));
    }

    virtual void run(const rapidjson::Value & value) const {
        for (const auto & i : validators)
            i->run(value);
    }

private:
    std::vector<std::unique_ptr<JsonValidatorBase>> validators;
};

/// This is basically just an immutable string.
struct StringWrapper {
    StringWrapper(const std::string & s)
            : s(s) {
    }
    const std::string & getString() const {
        return s;
    }

private:
    const std::string & s;
};

/// An interface for a json value validator.
/// Set it up with a string, and then supply some way of validating whether
/// the value is valid for the given string.
struct Validator : public StringWrapper {
    Validator(const std::string & s)
            : StringWrapper(s) {
    }
    virtual bool validate(const rapidjson::Value & value) const = 0;
};

/// Specialised Validator.
/// If the value does not contain a member matching the validator's string, an
/// exception is thrown.
/// Otherwise, validate returns true.
struct RequiredValidator : public Validator {
    RequiredValidator(const std::string & s)
            : Validator(s) {
    }
    bool validate(const rapidjson::Value & value) const {
        if (!value.HasMember(getString().c_str()))
            throw std::runtime_error("key " + getString() +
                                     " not found in config object");
        return true;
    }
};

/// Specialised Validator.
/// Returns true if the value contains a member matching the validator's string,
/// false otherwise.
struct OptionalValidator : public Validator {
    OptionalValidator(const std::string & s)
            : Validator(s) {
    }
    bool validate(const rapidjson::Value & value) const {
        return value.HasMember(getString().c_str());
    }
};

/// A class that can validate and mutate a stored, templated value based on a
/// supplied json value.
template <typename T>
struct JsonGetter {
    virtual bool check(const rapidjson::Value & value) const = 0;
    virtual void get(const rapidjson::Value & value) const = 0;
};

template <>
struct JsonGetter<double> {
    JsonGetter(double & t)
            : t(t) {
    }

    /// Returns true if value is a number, false otherwise.
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsNumber();
    }

    /// Gets the value as a double.
    virtual void get(const rapidjson::Value & value) const {
        t = value.GetDouble();
    }
    double & t;
};

template <>
struct JsonGetter<float> {
    JsonGetter(float & t)
            : t(t) {
    }

    /// Returns true if value is a number, false otherwise.
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsNumber();
    }

    /// Gets the value as a double, then casts it to float.
    virtual void get(const rapidjson::Value & value) const {
        t = value.GetDouble();
    }
    float & t;
};

template <>
struct JsonGetter<bool> {
    JsonGetter(bool & t)
            : t(t) {
    }

    /// Returns true if value is 'true' or 'false', false otherwise.
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsBool();
    }

    /// Converts json bool to C++ bool.
    virtual void get(const rapidjson::Value & value) const {
        t = value.GetBool();
    }
    bool & t;
};

template <>
struct JsonGetter<int> {
    JsonGetter(int & t)
            : t(t) {
    }

    /// Returns true if value is an integer, false otherwise.
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsInt();
    }

    /// Gets json value as integer.
    virtual void get(const rapidjson::Value & value) const {
        t = value.GetInt();
    }
    int & t;
};

/// General class for getting numerical json arrays into cl_floatx types
template <typename T, int LENGTH>
struct JsonArrayGetter {
    JsonArrayGetter(T & t)
            : t(t) {
    }

    /// Return true if value is an array of length LENGTH containing only
    /// numbers.
    virtual bool check(const rapidjson::Value & value) const {
        if (!value.IsArray())
            return false;
        if (value.Size() != LENGTH)
            return false;
        for (auto i = 0; i != LENGTH; ++i) {
            if (!value[i].IsNumber())
                return false;
        }
        return true;
    }

    /// Gets json value as a cl_floatx.
    virtual void get(const rapidjson::Value & value) const {
        for (auto i = 0; i != LENGTH; ++i) {
            t.s[i] = static_cast<cl_float>(value[i].GetDouble());
        }
    }

    T & t;
};

/// JsonGetter for cl_float3 is just a JsonArrayGetter with length 3
template <>
struct JsonGetter<cl_float3> : public JsonArrayGetter<cl_float3, 3> {
    JsonGetter(cl_float3 & t)
            : JsonArrayGetter(t) {
    }
};

/// JsonGetter for cl_float8 is just a JsonArrayGetter with length 8
template <>
struct JsonGetter<cl_float8> : public JsonArrayGetter<cl_float8, 8> {
    JsonGetter(cl_float8 & t)
            : JsonArrayGetter(t) {
    }
};

/// General class for getting a json field with strings mapped to enums
template <typename T>
struct JsonEnumGetter {
    JsonEnumGetter(T & t, const std::map<std::string, T> & m)
            : t(t)
            , stringkeys(m) {
    }

    /// Returns true if value is a string and is equal to an allowed string.
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsString() &&
               (any_of(stringkeys.begin(),
                       stringkeys.end(),
                       [&value](const std::pair<std::string, T> & i) {
                           return i.first == value.GetString();
                       }));
    }

    /// Look up the string in a dictionary and return the equivalent enum value.
    virtual void get(const rapidjson::Value & value) const {
        t = stringkeys.at(value.GetString());
    }

    T & t;
    const std::map<std::string, T> stringkeys;
};

/// JsonGetter for OutputMode is just a JsonEnumGetter with a specific map
template <>
struct JsonGetter<OutputMode> : public JsonEnumGetter<OutputMode> {
    JsonGetter(OutputMode & t)
            : JsonEnumGetter(t,
                             {{"all", ALL},
                              {"image_only", IMAGE_ONLY},
                              {"diffuse_only", DIFFUSE_ONLY}}) {
    }
};

template <typename T>
struct JsonGetter<std::vector<T>> {
    JsonGetter(std::vector<T> & t)
            : t(t) {
    }
    virtual bool check(const rapidjson::Value & value) const {
        return value.IsArray();
    }
    virtual void get(const rapidjson::Value & value) const {
        for (auto i = value.Begin(); i != value.End(); ++i) {
            T temp;
            ValueJsonValidator<T> getter(temp);
            getter.run(*i);
            t.push_back(temp);
        }
    }
    std::vector<T> & t;
};

template <typename T>
struct ValueJsonValidator : public JsonValidatorBase, public JsonGetter<T> {
    ValueJsonValidator(T & t)
            : JsonGetter<T>(t) {
    }

    virtual void run(const rapidjson::Value & value) const {
        if (!JsonGetter<T>::check(value)) {
            throw std::runtime_error("invalid value");
        }
        JsonGetter<T>::get(value);
    }
};

/// Combines the functionality of any templated JsonGetter with any Validator,
/// giving an object that, when run, validates that the value is present, checks
/// it, and gets it if possible.
/// It's possible to refer to instances in a typesafe way through pointers to
/// JsonValidatorBase, which allows for runtime-polymorphic JsonValidators to
/// be instantiated, depending on runtime requirements.
template <typename T, typename U>
struct FieldJsonValidator : public ValueJsonValidator<T>, public U {
    FieldJsonValidator(const std::string & s, T & t)
            : ValueJsonValidator<T>(t)
            , U(s) {
    }

    virtual void run(const rapidjson::Value & value) const {
        if (U::validate(value)) {
            ValueJsonValidator<T>::run(value[U::getString().c_str()]);
        }
    }
};
