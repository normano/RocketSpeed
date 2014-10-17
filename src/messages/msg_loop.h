// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
#pragma once

#include <map>
#include <memory>
#include "include/Env.h"
#include "src/messages/commands.h"
#include "src/messages/serializer.h"
#include "src/messages/messages.h"
#include "src/messages/event_loop.h"
#include "src/messages/msg_client.h"
#include "src/util/logging.h"
#include "src/util/log_buffer.h"
#include "src/util/auto_roll_logger.h"

namespace rocketspeed {

// Application callback are invoked with messages of this type
typedef std::function<void(std::unique_ptr<Message> msg)> MsgCallbackType;

class MsgLoop {
 public:
  // Create a listener to receive messages on a specified port.
  // When a message arrives, invoke the specified callback.
  MsgLoop(const Env* env,
          const EnvOptions& env_options,
          const HostId& hostid,
          const std::shared_ptr<Logger>& info_log,
          const std::map<MessageType, MsgCallbackType>& callbacks,
          CommandCallbackType command_callback = nullptr);

  virtual ~MsgLoop();

  // Start this instance of the Event Loop
  void Run(void);

  // Is the MsgLoop up and running?
  bool IsRunning() const { return event_loop_.IsRunning(); }

  // Stop the message loop.
  void Stop();

  // returns a client that is used to send messages to remote hosts
  MsgClient& GetClient() { return client_; }

  // Get the host ID of this message loop.
  const HostId& GetHostId() const { return hostid_; }

  // Send a command to the event loop for processing.
  // This call is thread-safe.
  Status SendCommand(std::unique_ptr<Command> command) {
    return event_loop_.SendCommand(std::move(command));
  }

 private:
  // The Environment
  const Env* env_;

  // The Environment Options
  const EnvOptions env_options_;

  // the host/port number of this Msg Loop
  HostId hostid_;

  // debug message go here
  const std::shared_ptr<Logger> info_log_;

  // The callbacks specified by the application
  const std::map<MessageType, MsgCallbackType> msg_callbacks_;

  // The underlying Eventloop callback handler
  EventLoop event_loop_;

  // A client that is used to respond to system Ping messages
  MsgClient client_;

  // The static method registered with the EventLoop
  static void EventCallback(EventCallbackContext ctx,
                            std::unique_ptr<Message> msg);

  // method to provide default handling of ping message
  void ProcessPing(std::unique_ptr<Message> msg);
  std::map<MessageType, MsgCallbackType> SanitizeCallbacks(
                  const std::map<MessageType, MsgCallbackType>& cb);
};

}  // namespace rocketspeed
