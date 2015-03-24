// AUTOGENERATED FILE - DO NOT MODIFY!
// This file generated by Djinni from rocketspeed.djinni

#pragma once

#include "optional.hpp"
#include "src-gen/djinni/cpp/ConfigurationImpl.hpp"
#include "src-gen/djinni/cpp/LogLevel.hpp"
#include "src-gen/djinni/cpp/MsgIdImpl.hpp"
#include "src-gen/djinni/cpp/PublishStatus.hpp"
#include "src-gen/djinni/cpp/Status.hpp"
#include "src-gen/djinni/cpp/SubscriptionRequestImpl.hpp"
#include "src-gen/djinni/cpp/SubscriptionStorage.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rocketspeed { namespace djinni {

class PublishCallbackImpl;
class ReceiveCallbackImpl;
class SnapshotCallbackImpl;
class SubscribeCallbackImpl;
class WakeLockImpl;

class ClientImpl {
public:
    virtual ~ClientImpl() {}

    static std::shared_ptr<ClientImpl> Create(LogLevel log_level, ConfigurationImpl config, int32_t tenant_id, std::string client_id, std::shared_ptr<SubscribeCallbackImpl> subscribe_callback, SubscriptionStorage storage, std::shared_ptr<WakeLockImpl> wake_lock);

    virtual Status Start(std::shared_ptr<ReceiveCallbackImpl> receive_callback, bool restore_subscriptions, bool resubscribe_from_storage) = 0;

    virtual PublishStatus Publish(std::string namespace_id, std::string topic_name, std::vector<uint8_t> data, std::experimental::optional<MsgIdImpl> message_id, std::shared_ptr<PublishCallbackImpl> publish_callback) = 0;

    virtual void ListenTopics(std::vector<SubscriptionRequestImpl> names) = 0;

    virtual void Acknowledge(std::string namespace_id, std::string topic_name, int64_t sequence_number) = 0;

    virtual void SaveSubscriptions(std::shared_ptr<SnapshotCallbackImpl> snapshot_callback) = 0;

    virtual void Close() = 0;
};

} }  // namespace rocketspeed::djinni
