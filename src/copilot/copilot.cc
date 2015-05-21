// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
#define __STDC_FORMAT_MACROS
#include "src/copilot/copilot.h"
#include <map>
#include <string>
#include <thread>
#include <vector>
#include "src/client/client.h"
#include "src/util/memory.h"
#include "src/util/common/fixed_configuration.h"

namespace rocketspeed {

/**
 * Sanitize user-specified options
 */
CopilotOptions Copilot::SanitizeOptions(CopilotOptions options) {
  if (options.info_log == nullptr) {
    Status s = CreateLoggerFromOptions(options.env,
                                       options.log_dir,
                                       "LOG.copilot",
                                       options.log_file_time_to_roll,
                                       options.max_log_file_size,
                                       options.info_log_level,
                                       &options.info_log);
    if (!s.ok()) {
      // No place suitable for logging
      options.info_log = std::make_shared<NullLogger>();
    }
  }

  return std::move(options);
}

/**
 * Private constructor for a Copilot
 */
Copilot::Copilot(CopilotOptions options, std::unique_ptr<ClientImpl> client):
  options_(SanitizeOptions(std::move(options))) {
  stats_.resize(options_.msg_loop->GetNumWorkers());
  options_.msg_loop->RegisterCallbacks(InitializeCallbacks());

  // Create workers.
  std::shared_ptr<ControlTowerRouter> router =
    std::make_shared<ControlTowerRouter>(options_.control_towers,
                                         options_.consistent_hash_replicas,
                                         options_.control_tower_connections);
  for (int i = 0; i < options_.msg_loop->GetNumWorkers(); ++i) {
    workers_.emplace_back(new CopilotWorker(options_,
                                            router,
                                            i,
                                            this));
  }
  // Create Rollcall topic writer
  if (options_.rollcall_enabled) {
    rollcall_.reset(new RollcallImpl(std::move(client),
                                     InvalidTenant,
                                     InvalidNamespace,
                                     0,
                                     nullptr));
  }

  LOG_INFO(options_.info_log, "Created a new Copilot");
  options_.info_log->Flush();
}

Copilot::~Copilot() {
  // Must be stopped first.
  assert(workers_.empty());
  options_.info_log->Flush();
}

void Copilot::Stop() {
  assert(!options_.msg_loop->IsRunning());

  workers_.clear();
  options_.log_router.reset();
}

/**
 * This is a static method to create a Copilot
 */
Status Copilot::CreateNewInstance(CopilotOptions options,
                                  Copilot** copilot) {
  if (!options.log_router) {
    assert(false);
    return Status::InvalidArgument("Log router must be provided");
  }
  // Publishing to the rollcall topic needs a pilot.
  if (options.pilots.size() <= 0) {
    assert(options.pilots.size());
    return Status::InvalidArgument("At least one pilot much be provided.");
  }
  // Create a configuration to determine the identity of a pilot.
  // Use a dummy copilot identifier, this is not needed and can be
  // removed in the future.
  ClientOptions client_options;
  client_options.config =
    std::make_shared<FixedConfiguration>(options.pilots[0], HostId());

  // Create a client to write rollcall topic.
  std::unique_ptr<ClientImpl> client;
  Status status = ClientImpl::Create(std::move(client_options),
                                     &client,
                                     true);
  if (!status.ok()) {
    return status;
  }
  *copilot = new Copilot(std::move(options), std::move(client));
  return Status::OK();
}

// A static callback method to process MessageData
void Copilot::ProcessDeliver(std::unique_ptr<Message> msg, StreamID origin) {
  options_.msg_loop->ThreadCheck();

  const int event_loop_worker = options_.msg_loop->GetThreadWorkerIndex();

  // get the request message
  MessageData* data = static_cast<MessageData*>(msg.get());

  LOG_INFO(options_.info_log,
      "Received data (%.16s)@%" PRIu64 " in ns(%s) for Topic(%s)",
      data->GetPayload().ToString().c_str(),
      data->GetSequenceNumber(),
      data->GetNamespaceId().ToString().c_str(),
      data->GetTopicName().ToString().c_str());

  // map the topic to a logid
  LogID logid;
  Status st = options_.log_router->GetLogID(data->GetNamespaceId(),
                                            data->GetTopicName(),
                                            &logid);
  if (!st.ok()) {
    LOG_INFO(options_.info_log,
        "Unable to map msg to logid %s", st.ToString().c_str());
    return;
  }

  // calculate the destination worker
  int worker_id = static_cast<int>(logid % options_.msg_loop->GetNumWorkers());
  auto& worker = workers_[worker_id];

  // forward message to worker
  if (!worker->Forward(logid, std::move(msg), event_loop_worker, origin)) {
    LOG_WARN(options_.info_log,
        "Worker %d queue is full.",
        static_cast<int>(worker_id));
  }
}

// A static callback method to process MessageMetadata
void Copilot::ProcessMetadata(std::unique_ptr<Message> msg, StreamID origin) {
  options_.msg_loop->ThreadCheck();

  // get the request message
  MessageMetadata* request = static_cast<MessageMetadata*>(msg.get());

  // Process each topic
  for (size_t i = 0; i < request->GetTopicInfo().size(); i++) {
    // map the topic to a logid
    const TopicPair& topic = request->GetTopicInfo()[i];

    LOG_INFO(options_.info_log,
      "Received %s %s for Topic(%s)@%" PRIu64,
      topic.topic_type == MetadataType::mSubscribe
        ? "subscribe" : "unsubscribe",
      request->GetMetaType() == MessageMetadata::MetaType::Request
        ? "request" : "response",
      topic.topic_name.c_str(),
      topic.seqno);

    LogID logid;
    Status st = options_.log_router->GetLogID(topic.namespace_id,
                                              topic.topic_name,
                                              &logid);
    if (!st.ok()) {
      LOG_INFO(options_.info_log,
          "Unable to map msg to logid %s", st.ToString().c_str());
      continue;
    }
    // calculate the destination worker
    int worker_id =
        static_cast<int>(logid % options_.msg_loop->GetNumWorkers());
    auto& worker = workers_[worker_id];

    // Copy out only the ith topic into a new message.
    MessageMetadata* newmsg = new MessageMetadata(
                            request->GetTenantID(),
                            request->GetMetaType(),
                            std::vector<TopicPair> {topic});
    std::unique_ptr<Message> newmessage(newmsg);

    // forward message to worker
    int event_loop_worker = options_.msg_loop->GetThreadWorkerIndex();
    worker->Forward(logid, std::move(newmessage), event_loop_worker, origin);
  }
}

void Copilot::ProcessGap(std::unique_ptr<Message> msg, StreamID origin) {
  options_.msg_loop->ThreadCheck();

  const int event_loop_worker = options_.msg_loop->GetThreadWorkerIndex();

  // get the gap message
  MessageGap* gap = static_cast<MessageGap*>(msg.get());

  LOG_INFO(options_.info_log,
      "Received gap %" PRIu64 "-%" PRIu64 " for Topic(%s)",
      gap->GetStartSequenceNumber(),
      gap->GetEndSequenceNumber(),
      gap->GetTopicName().c_str());

  // map the topic to a logid
  LogID logid;
  Status st = options_.log_router->GetLogID(gap->GetNamespaceId(),
                                            gap->GetTopicName(),
                                            &logid);
  if (!st.ok()) {
    LOG_INFO(options_.info_log,
        "Unable to map msg to logid %s", st.ToString().c_str());
    return;
  }

  // calculate the destination worker
  int worker_id = static_cast<int>(logid % options_.msg_loop->GetNumWorkers());
  auto& worker = workers_[worker_id];

  // forward message to worker
  if (!worker->Forward(logid, std::move(msg), event_loop_worker, origin)) {
    LOG_WARN(options_.info_log,
        "Worker %d queue is full.",
        static_cast<int>(worker_id));
  }
}

void Copilot::ProcessSubscribe(std::unique_ptr<Message> msg, StreamID origin) {
  options_.msg_loop->ThreadCheck();

  auto subscribe = static_cast<MessageSubscribe*>(msg.get());
  LOG_INFO(options_.info_log,
           "Received subscribe request for Topic(%s, %s)@%" PRIu64,
           subscribe->GetNamespace().c_str(),
           subscribe->GetTopicName().c_str(),
           subscribe->GetStartSequenceNumber());

  // Calculate log ID for this topic.
  LogID logid;
  Status st = options_.log_router->GetLogID(subscribe->GetNamespace(),
                                            subscribe->GetTopicName(),
                                            &logid);
  if (!st.ok()) {
    LOG_WARN(options_.info_log,
             "Unable to map Topic(%s, %s) to LogID: %s",
             subscribe->GetNamespace().c_str(),
             subscribe->GetTopicName().c_str(),
             st.ToString().c_str());
    return;
  }

  // Calculate the destination worker.
  auto dest_worker_id =
      static_cast<int>(logid % options_.msg_loop->GetNumWorkers());
  auto& worker = workers_[dest_worker_id];

  // Forward message to responsible worker.
  auto worker_id = options_.msg_loop->GetThreadWorkerIndex();
  if (!worker->Forward(logid, std::move(msg), worker_id, origin)) {
    LOG_WARN(options_.info_log, "Worker %d queue is full.", worker_id);
  }
}

void Copilot::ProcessUnsubscribe(std::unique_ptr<Message> msg,
                                 StreamID origin) {
  options_.msg_loop->ThreadCheck();

  auto unsubscribe = static_cast<MessageUnsubscribe*>(msg.get());
  LOG_INFO(options_.info_log,
           "Received unsubscribe for subscription (%" PRIu64
           ") at stream (%llu)",
           unsubscribe->GetSubID(),
           origin);

  // Broadcast to all workers.
  for (int i = 0; i < options_.msg_loop->GetNumWorkers(); ++i) {
    std::unique_ptr<MessageUnsubscribe> new_msg(new MessageUnsubscribe(
        unsubscribe->GetTenantID(),
        unsubscribe->GetSubID(),
        unsubscribe->GetReason()));
    LogID logid = 0;  // unused
    int event_loop_worker = options_.msg_loop->GetThreadWorkerIndex();
    workers_[i]->Forward(logid,
                         std::move(new_msg),
                         event_loop_worker,
                         origin);
  }
}

void Copilot::ProcessGoodbye(std::unique_ptr<Message> msg, StreamID origin) {
  options_.msg_loop->ThreadCheck();

  MessageGoodbye* goodbye = static_cast<MessageGoodbye*>(msg.get());
  switch (goodbye->GetOriginType()) {
    case MessageGoodbye::OriginType::Client: {
      LOG_INFO(options_.info_log, "Received goodbye for client %llu", origin);
      break;
    }

    case MessageGoodbye::OriginType::Server: {
      LOG_WARN(options_.info_log, "Received goodbye for server %llu", origin);
      break;
    }
  }

  // Inform all workers.
  for (int i = 0; i < options_.msg_loop->GetNumWorkers(); ++i) {
    std::unique_ptr<Message> new_msg(
      new MessageGoodbye(goodbye->GetTenantID(),
                         goodbye->GetCode(),
                         goodbye->GetOriginType()));
    LogID logid = 0;  // unused
    int event_loop_worker = options_.msg_loop->GetThreadWorkerIndex();
    workers_[i]->Forward(logid,
                         std::move(new_msg),
                         event_loop_worker,
                         origin);
  }
}

// A static method to initialize the callback map
std::map<MessageType, MsgCallbackType> Copilot::InitializeCallbacks() {
  using namespace std::placeholders;
  // create a temporary map and initialize it
  std::map<MessageType, MsgCallbackType> cb;
  cb[MessageType::mDeliver] = [this] (std::unique_ptr<Message> msg,
                                      StreamID origin) {
    ProcessDeliver(std::move(msg), origin);
  };
  cb[MessageType::mMetadata] = [this] (std::unique_ptr<Message> msg,
                                       StreamID origin) {
    ProcessMetadata(std::move(msg), origin);
  };
  cb[MessageType::mGap] = [this] (std::unique_ptr<Message> msg,
                                  StreamID origin) {
    ProcessGap(std::move(msg), origin);
  };
  cb[MessageType::mGoodbye] = [this] (std::unique_ptr<Message> msg,
                                      StreamID origin) {
    ProcessGoodbye(std::move(msg), origin);
  };
  cb[MessageType::mSubscribe] =
      std::bind(&Copilot::ProcessSubscribe, this, _1, _2);
  cb[MessageType::mUnsubscribe] =
      std::bind(&Copilot::ProcessUnsubscribe, this, _1, _2);
  return cb;
}

int Copilot::GetLogWorker(LogID logid, const HostId& control_tower) const {
  // Hash control tower to a worker.
  const int num_workers = options_.msg_loop->GetNumWorkers();
  const size_t connection = logid % options_.control_tower_connections;
  const size_t hash = MurmurHash2<std::string, size_t>()(control_tower.hostname,
                                                         control_tower.port);
  return static_cast<int>((hash + connection) % num_workers);
}

Statistics Copilot::GetStatisticsSync() const {
  return options_.msg_loop->AggregateStatsSync(
    [this] (int i) { return stats_[i].all; }
  );
}

Status Copilot::UpdateControlTowers(
    std::unordered_map<uint64_t, HostId> nodes) {
  Status result;
  // Send the new nodes to all workers.
  // If we fail to forward to any single worker then return failure so that
  // whoever is providing the updated hosts can retry later.
  std::shared_ptr<ControlTowerRouter> new_router =
    std::make_shared<ControlTowerRouter>(std::move(nodes),
                                         options_.consistent_hash_replicas,
                                         options_.control_tower_connections);
  for (int i = 0; i < options_.msg_loop->GetNumWorkers(); ++i) {
    if (!workers_[i]->Forward(new_router)) {
      LOG_WARN(options_.info_log,
        "Failed to forward control tower update to worker %" PRIu32, i);
      result = Status::NoBuffer();
    }
  }
  return result;
}

}  // namespace rocketspeed
