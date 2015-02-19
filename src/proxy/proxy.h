// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "include/Types.h"
#include "src/messages/messages.h"
#include "src/messages/msg_loop.h"
#include "src/util/common/statistics.h"
#include "src/proxy/options.h"

#pragma GCC visibility push(default)
namespace rocketspeed {

/**
 * The RocketSpeed Proxy is a library that appears as Pilot and Copilot to the
 * outside, but internally forwards all messages to a real RocketSpeed service.
 *
 * The proxy library is suitable for embedding in external services that need
 * to route traffic to RocketSpeed on behalf of multiple clients.
 */
class Proxy {
 public:
  typedef std::function<void(const ClientID&, std::string)>
    OnMessageCallback;

  typedef std::function<void(const std::vector<ClientID>&)>
    OnDisconnectCallback;

  /**
   * Creates a new instance of the RocketSpeed proxy.
   *
   * @param options Configuration of the proxy.
   * @param param Output parameter for the created proxy object.
   * @return ok() if successful, otherwise an error status.
   */
  static Status CreateNewInstance(ProxyOptions options,
                                  std::unique_ptr<Proxy>* proxy);

  /**
   * Starts the proxy service.
   *
   * @param on_message Callback for when a message is received from the
   *                   RocketSpeed service. The arguments are the client ID
   *                   of the intended recipient, and the RocketSpeed
   *                   serialized message.
   * @param on_disconnect Callback for when a connection to the RocketSpeed
   *                      service breaks. The arguments are the list of
   *                      affected client IDs. Those clients should re-issue
   *                      any subscriptions sent through the proxy.
   * @return ok() if successful, otherwise an error status.
   */
  Status Start(OnMessageCallback on_message,
               OnDisconnectCallback on_disconnect);

  /**
   * Sends a message to a RocketSpeed pilot or copilot on behalf of a client.
   * Data messages are sent to a pilot, while metadata messages are sent to a
   * copilot.
   *
   * If the proxy has sent all messages before the provided sequence number,
   * then the message is forwarded immediately. If not, then it is buffered
   * and sent later. If not enough buffer space is available then an error
   * status will be returned.
   *
   * Sequence numbers must start at 0.
   *
   * If a sequence number of -1 is provided, the message will
   * be sent immediately. This should be used if the caller
   * can guarantee ordering.
   *
   * @param msg The serialized RocketSpeed message.
   * @param session Unique session ID. Messages are ordered per session.
   * @param sequence Sequence ID of messages per session.
   * @return ok() if successful, otherwise an error status.
   */
  Status Forward(std::string msg, int64_t session, int32_t sequence);

  /**
   * Instructs the proxy to reset the next expected sequence number for a
   * session to 0, effectively removing all state for that session. Failure
   * to destroy sessions will result in space leaks within the proxy.
   *
   * @param session Unique session ID to destroy.
   * @return ok() if successful, otherwise an error status.
   */
  Status DestroySession(int64_t session);

  /**
   * Stops the proxy service. May block while waiting for the event loop
   * thread to join.
   */
  ~Proxy();

  const Statistics& GetStatistics() const {
    return stats_.all;
  }

 private:
  explicit Proxy(ProxyOptions options);

  OnMessageCallback on_message_;
  OnDisconnectCallback on_disconnect_;
  std::shared_ptr<Logger> info_log_;
  BaseEnv* env_;
  std::shared_ptr<Configuration> config_;
  std::unique_ptr<MsgLoop> msg_loop_;
  Env::ThreadId msg_thread_;

  struct Stats {
    Stats() {
      forwards = all.AddCounter("rocketspeed.proxy.forwards");
      forward_errors = all.AddCounter("rocketspeed.proxy.forward_errors");
      on_message_calls = all.AddCounter("rocketspeed.proxy.on_message_calls");
    }

    Statistics all;

    Counter* forwards;
    Counter* forward_errors;
    Counter* on_message_calls;
  } stats_;
};

}  // namespace rocketspeed
#pragma GCC visibility pop
