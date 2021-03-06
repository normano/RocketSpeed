// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#pragma once

#include "src-gen/djinni/cpp/MsgId.hpp"
#include "src-gen/djinni/cpp/Status.hpp"
#include <cstdint>
#include <string>

namespace rocketspeed { namespace djinni {

class PublishCallback {
public:
    virtual ~PublishCallback() {}

    virtual void Call(MsgId message_id, std::string namespace_id, std::string topic_name, int64_t seqno, Status status) = 0;
};

} }  // namespace rocketspeed::djinni
