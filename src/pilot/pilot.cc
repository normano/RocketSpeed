//  Copyright (c) 2014, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#include "src/pilot/pilot.h"
#include <map>
#include <string>

namespace rocketspeed {

/**
 * Sanitize user-specified options
 */
PilotOptions Pilot::SanitizeOptions(PilotOptions options) {
  if (options.info_log == nullptr) {
    Status s = CreateLoggerFromOptions(options.env,
                                       options.log_dir,
                                       options.log_file_time_to_roll,
                                       options.max_log_file_size,
                                       options.info_log_level,
                                       &options.info_log);
    if (!s.ok()) {
      // No place suitable for logging
      options.info_log = nullptr;
    }
  }

  return std::move(options);
}

void Pilot::Run() {
  msg_loop_.Run();
}

/**
 * Private constructor for a Pilot
 */
Pilot::Pilot(PilotOptions options,
             const Configuration& conf):
  options_(SanitizeOptions(std::move(options))),
  conf_(conf),
  callbacks_(InitializeCallbacks()),
  msg_loop_(options_.env,
            options_.env_options,
            HostId(options_.pilotname, options_.port_number),
            options_.info_log,
            static_cast<ApplicationCallbackContext>(this),
            callbacks_),
  log_storage_(std::move(options_.log_storage)),
  log_router_(options_.log_count) {
  Log(InfoLogLevel::INFO_LEVEL, options_.info_log,
      "Created a new Pilot");
  options_.info_log->Flush();
}

Pilot::~Pilot() {
}

/**
 * This is a static method to create a Pilot
 */
Status Pilot::CreateNewInstance(PilotOptions options,
                                const Configuration& conf,
                                Pilot** pilot) {
  *pilot = new Pilot(std::move(options), conf);

  // Ensure we managed to connect to the log storage.
  if ((*pilot)->log_storage_ == nullptr) {
    delete *pilot;
    *pilot = nullptr;
    return Status::NotInitialized();
  }

  return Status::OK();
}

// A static callback method to process MessageData
void Pilot::ProcessData(ApplicationCallbackContext ctx,
                        std::unique_ptr<Message> msg) {
  Pilot* pilot = static_cast<Pilot*>(ctx);

  // Sanity checks.
  assert(msg);
  assert(msg->GetMessageType() == MessageType::mData);

  // Route topic to log ID.
  MessageData const* msgData = static_cast<MessageData const*>(msg.get());
  LogID logid;
  std::string topicName = msgData->GetTopicName().ToString();
  Slice payload = msgData->GetPayload();
  Retention retention = Retention::OneHour;
  if (!pilot->log_router_.GetLogID(topicName, retention, &logid).ok()) {
    // Failed to route topic to log ID.
    Log(InfoLogLevel::WARN_LEVEL, pilot->options_.info_log,
      "Failed to route topic '%s' with retention %d to a log",
      topicName.c_str(), static_cast<int>(retention));
    return;
  }

  // Append to log storage.
  auto status = pilot->log_storage_->Append(logid, payload);
  if (!status.ok()) {
    Log(InfoLogLevel::WARN_LEVEL, pilot->options_.info_log,
      "Failed to append to log ID %lu",
      static_cast<uint64_t>(logid));
    return;
  }

  // Debug message.
  fprintf(stdout,
          "Pilot::ProcessData '%s' %u bytes to log %lu\n",
          topicName.c_str(),
          static_cast<unsigned int>(payload.size()),
          static_cast<uint64_t>(logid));
}

// A static method to initialize the callback map
std::map<MessageType, MsgCallbackType> Pilot::InitializeCallbacks() {
  // create a temporary map and initialize it
  std::map<MessageType, MsgCallbackType> cb;
  cb[MessageType::mData] = MsgCallbackType(&ProcessData);

  // return the updated map
  return cb;
}
}  // namespace rocketspeed
