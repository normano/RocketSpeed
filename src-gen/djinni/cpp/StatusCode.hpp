// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#pragma once

#include <functional>

namespace rocketspeed { namespace djinni {

enum class StatusCode : int {
    OK,
    NOTFOUND,
    NOTSUPPORTED,
    INVALIDARGUMENT,
    IOERROR,
    NOTINITIALIZED,
    INTERNAL,
    UNAUTHORIZED,
    TIMEDOUT,
};

} }  // namespace rocketspeed::djinni

namespace std {

template <>
struct hash<::rocketspeed::djinni::StatusCode> {
    size_t operator()(::rocketspeed::djinni::StatusCode type) const {
        return std::hash<int>()(static_cast<int>(type));
    }
};

}  // namespace std
