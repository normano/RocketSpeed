// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#pragma once

#include <cstdint>
#include <vector>

namespace rocketspeed { namespace djinni {

class MessageCallback {
public:
    virtual ~MessageCallback() {}

    virtual void Call(int64_t session_id, std::vector<uint8_t> message) = 0;
};

} }  // namespace rocketspeed::djinni