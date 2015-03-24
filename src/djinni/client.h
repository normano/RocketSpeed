// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
#pragma once

#include <memory>

#include "include/RocketSpeed.h"

#include "src-gen/djinni/cpp/ClientImpl.hpp"
#include "src-gen/djinni/cpp/MsgIdImpl.hpp"
#include "src-gen/djinni/cpp/PublishStatus.hpp"
#include "src-gen/djinni/cpp/RetentionBase.hpp"
#include "src-gen/djinni/cpp/Status.hpp"
#include "src-gen/djinni/cpp/SubscriptionRequestImpl.hpp"

namespace rocketspeed {
namespace djinni {

class PublishCallbackImpl;
class SnapshotCallbackImpl;

class Client : public ClientImpl {
 public:
  explicit Client(std::unique_ptr<rocketspeed::Client> client,
                  rocketspeed::SubscribeCallback subscribe_callback)
      : client_(std::move(client))
      , subscribe_callback_(std::move(subscribe_callback)) {
  }

  Status Start(std::shared_ptr<ReceiveCallbackImpl> receive_callback,
               bool restore_subscriptions,
               bool resubscribe_from_storage) override;

  PublishStatus Publish(
      std::string namespace_id,
      std::string topic_name,
      std::vector<uint8_t> data,
      std::experimental::optional<MsgIdImpl> message_id,
      std::shared_ptr<PublishCallbackImpl> publish_callback) override;

  void ListenTopics(std::vector<SubscriptionRequestImpl> names) override;

  void Acknowledge(std::string namespace_id,
                   std::string topic_name,
                   int64_t sequence_number) override;

  void SaveSubscriptions(
      std::shared_ptr<SnapshotCallbackImpl> subscriptions_callback) override;

  void Close() override;

 private:
  std::unique_ptr<rocketspeed::Client> client_;
  /** Stores callback pointer between client creation and startup. */
  rocketspeed::SubscribeCallback subscribe_callback_;
};

}  // namespace djinni
}  // namespace rocketspeed
