// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
#include "src/proxy/proxy.h"

#include <climits>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include "src/util/common/ordered_processor.h"
#include "src/util/worker_loop.h"

namespace rocketspeed {

/**
 * Bidirectional map between hosts and sessions, i.e. what pilots and
 * copilots a session is communicating with.
 * This is used to inform clients when the host goes down.
 */
struct HostSessionMatrix {
  /**
   * Add a (session, host) into the matrix. Has no effect if the pair
   * has already been inserted.
   *
   * @param session The session communicating with a host.
   * @param host The host the session is communicating with.
   */
  void Add(int64_t session, const ClientID& host) {
    host_to_sessions_[host].insert(session);
    auto& hosts = session_to_hosts_[session];
    if (std::find(hosts.begin(), hosts.end(), host) == hosts.end()) {
      hosts.push_back(host);
    }
  }

  /**
   * Remove a host column from matrix.
   *
   * @param host The host to remove.
   * @return The sessions for that host.
   */
  std::unordered_set<int64_t> RemoveHost(const ClientID& host) {
    auto sessions = std::move(host_to_sessions_[host]);
    host_to_sessions_.erase(host);
    for (int64_t session : sessions) {
      auto& hosts = session_to_hosts_[session];
      auto it = std::find(hosts.begin(), hosts.end(), host);
      if (it != hosts.end()) {
        hosts.erase(it);
      }
    }
    return sessions;
  }

  /**
   * Remove a session row from matrix.
   *
   * @param session The session to remove.
   */
  void RemoveSession(int64_t session) {
    const auto& hosts = session_to_hosts_[session];
    for (const ClientID& host : hosts) {
      host_to_sessions_[host].erase(session);
    }
    session_to_hosts_.erase(session);
  }

 private:
  // Maps host to sessions communicating with this host.
  std::unordered_map<ClientID, std::unordered_set<int64_t>> host_to_sessions_;

  // Maps sessions to the hosts they are communicating with.
  // Each session should only communicate with up to two hosts (pilot, copilot).
  std::unordered_map<int64_t, std::vector<ClientID>> session_to_hosts_;
};

// Worker thread for processing forwarded messages.
class ProxyWorker {
 public:
  ProxyWorker(MsgLoop* msg_loop,
              Proxy::OnDisconnectCallback on_disconnect,
              uint32_t max_tasks,
              int buffer_size)
  : worker_loop_(max_tasks)
  , buffer_size_(buffer_size)
  , msg_loop_(msg_loop)
  , on_disconnect_(std::move(on_disconnect)) {
  }

  void Start() {
    // Worker loop processes a serialized stream of tasks.
    // A task is a (session, seqno, command).
    // Each task is processed by an OrderedProcessor per session.
    worker_loop_.Run([this] (Task task) {
      // Find session in map.
      int64_t session = task.session;
      auto it = sessions_.find(session);
      if (task.command == nullptr) {
        if (task.session != -1) {
          // Remove session.
          if (it != sessions_.end()) {
            sessions_.erase(it);
          }
          host_session_matrix_.RemoveSession(session);
        } else {
          // Remove host.
          auto sessions = host_session_matrix_.RemoveHost(task.host);
          std::vector<int64_t> sessions_vec(sessions.begin(), sessions.end());
          if (on_disconnect_) {
            on_disconnect_(std::move(sessions_vec));
          }
        }
      } else {
        if (it == sessions_.end()) {
          // Not there, so create it.
          SessionProcessor processor(
            buffer_size_,
            [this, session] (std::unique_ptr<Command> command) {
              // Process command by sending it to the event loop.
              // Need to check if session is still there. Previous command
              // processed may have caused it to drop.s
              if (sessions_.find(session) != sessions_.end()) {
                // TODO(pja) 1 : Use other threads based on session.
                Status st = msg_loop_->SendCommand(std::move(command));
                if (!st.ok() && on_disconnect_) {
                  on_disconnect_( { session } );
                  sessions_.erase(session);
                }
              }
            });

          auto result = sessions_.emplace(task.session, std::move(processor));
          assert(result.second);
          it = result.first;
        }
        host_session_matrix_.Add(session, task.host);
        Status st = it->second.Process(std::move(task.command), task.seqno);
        if (!st.ok() && on_disconnect_) {
          on_disconnect_( { session } );
          sessions_.erase(it);
        }
      }
    });
  }

  void Stop() {
    worker_loop_.Stop();
  }

  Status EnqueueCommand(int64_t session,
                        int32_t seqno,
                        std::unique_ptr<Command> cmd,
                        ClientID host) {
    return worker_loop_.Send(session, seqno, std::move(cmd), std::move(host)) ?
           Status::OK() : Status::NoBuffer();
  }

  Status EnqueueRemoveSession(int64_t session) {
    return worker_loop_.Send(session, 0, nullptr, ClientID("")) ?
           Status::OK() : Status::NoBuffer();
  }

  Status EnqueueRemoveHost(ClientID host) {
    return worker_loop_.Send(-1, 0, nullptr, std::move(host)) ?
           Status::OK() : Status::NoBuffer();
  }

 private:
  typedef OrderedProcessor<std::unique_ptr<Command>> SessionProcessor;

  struct Task {
    Task(int64_t _session,
         int32_t _seqno,
         std::unique_ptr<Command> _cmd,
         ClientID _host)
    : session(_session)
    , seqno(_seqno)
    , command(std::move(_cmd))
    , host(std::move(_host)) {}

    Task() {}

    int64_t session;
    int32_t seqno;
    std::unique_ptr<Command> command;
    ClientID host;
  };

  std::unordered_map<int64_t, SessionProcessor> sessions_;
  WorkerLoop<Task> worker_loop_;
  const int buffer_size_;
  MsgLoop* msg_loop_;
  Proxy::OnDisconnectCallback on_disconnect_;
  HostSessionMatrix host_session_matrix_;
};


Status Proxy::CreateNewInstance(ProxyOptions options,
                                std::unique_ptr<Proxy>* proxy) {
  // Sanitize / Validate options.
  if (options.info_log == nullptr) {
    options.info_log = std::make_shared<NullLogger>();
  }

  if (options.conf == nullptr) {
    return Status::InvalidArgument("Configuration required");
  }

  if (options.num_workers <= 0) {
    return Status::InvalidArgument("Invalid number of workers");
  }

  // Create the proxy object.
  proxy->reset(new Proxy(std::move(options)));
  return Status::OK();
}

Proxy::Proxy(ProxyOptions options)
: info_log_(std::move(options.info_log))
, env_(options.env)
, config_(std::move(options.conf))
, msg_thread_(0)
, worker_thread_(0) {
  msg_loop_.reset(new MsgLoop(env_,
                              options.env_options,
                              0,  // port
                              options.num_workers,
                              info_log_,
                              "proxy"));

  // Message callback.
  // Regardless of type, just forward back to the host.
  auto callback = [this] (std::unique_ptr<Message> msg) {
    if (on_message_) {
      LOG_INFO(info_log_,
        "Received message from RocketSpeed, type %d",
        static_cast<int>(msg->GetMessageType()));

      // TODO(pja) 1 : ideally we wouldn't reserialize here.
      std::string serial;
      msg->SerializeToString(&serial);

      // Parse origin as session.
      const char* origin = msg->GetOrigin().c_str();
      int64_t session = strtoll(origin, nullptr, 10);

      // strtoll failure modes are:
      // return 0LL if could not convert.
      // return LLONG_MIN/MAX if out of range, with errno set to ERANGE.
      if ((session == 0 && strcmp(origin, "0")) ||
          (session == LLONG_MIN && errno == ERANGE) ||
          (session == LLONG_MAX && errno == ERANGE)) {
        LOG_ERROR(info_log_,
          "Could not parse message origin '%s' into a session ID.",
          origin);
        stats_.bad_origins->Add(1);
      } else {
        on_message_(session, std::move(serial));
        stats_.on_message_calls->Add(1);
      }
    }
  };

  // Need to handle goodbyes specially.
  auto goodbye_callback = [this] (std::unique_ptr<Message> msg) {
    MessageGoodbye* goodbye = static_cast<MessageGoodbye*>(msg.get());
    if (goodbye->GetOriginType() == MessageGoodbye::OriginType::Server) {
      LOG_WARN(info_log_,
        "Received goodbye from server %s.",
        goodbye->GetOrigin().c_str());
      worker_->EnqueueRemoveHost(goodbye->GetOrigin());
    } else {
      LOG_WARN(info_log_,
        "Proxy received client goodbye from %s, but has no clients.",
        goodbye->GetOrigin().c_str());
    }
  };

  // Use same callback for all server-generated messages.
  std::map<MessageType, MsgCallbackType> callbacks;
  callbacks[MessageType::mMetadata] = callback;
  callbacks[MessageType::mDataAck] = callback;
  callbacks[MessageType::mGap] = callback;
  callbacks[MessageType::mDeliver] = callback;
  callbacks[MessageType::mPing] = callback;

  // Except goodbye. Goodbye needs to be handled separately.
  callbacks[MessageType::mGoodbye] = goodbye_callback;
  msg_loop_->RegisterCallbacks(callbacks);
}

Status Proxy::Start(OnMessageCallback on_message,
                    OnDisconnectCallback on_disconnect) {
  on_message_ = std::move(on_message);
  on_disconnect_ = std::move(on_disconnect);
  msg_thread_ = env_->StartThread([this] () { msg_loop_->Run(); },
                                  "proxy");

  worker_.reset(new ProxyWorker(msg_loop_.get(),
                                on_disconnect_,
                                1 << 20,  // proxy worker queue size
                                16));     // max out of order messages

  worker_thread_ = env_->StartThread([this] () { worker_->Start(); },
                                     "proxyw");
  return msg_loop_->WaitUntilRunning();
}

Status Proxy::Forward(std::string msg, int64_t session, int32_t sequence) {
  stats_.forwards->Add(1);

  // TODO(pja) 1 : Really inefficient. Only need to deserialize header,
  // not entire message, and don't need to copy entire message.
  std::unique_ptr<char[]> buffer = Slice(msg).ToUniqueChars();
  std::unique_ptr<Message> message =
    Message::CreateNewInstance(std::move(buffer), msg.size());

  // Internally the session is out client ID.
  message->SetOrigin(std::to_string(session));
  message->SerializeToString(&msg);

  if (!message) {
    LOG_ERROR(info_log_,
      "Failed to deserialize message forwarded to proxy.");
    stats_.forward_errors->Add(1);
    return Status::InvalidArgument("Message failed to deserialize");
  }

  // Select destination based on message type.
  HostId const* host_id = nullptr;
  switch (message->GetMessageType()) {
    case MessageType::mPing:  // could go to either
    case MessageType::mPublish:
      host_id = &config_->GetPilotHostIds().front();
      break;

    case MessageType::mMetadata:
      host_id = &config_->GetCopilotHostIds().front();
      break;

    case MessageType::mDataAck:
    case MessageType::mGap:
    case MessageType::mDeliver:
    case MessageType::NotInitialized:
    default:
      // Client shouldn't be sending us these kinds of messages.
      LOG_ERROR(info_log_,
        "Client %s attempting to send invalid message type through proxy (%d)",
        message->GetOrigin().c_str(),
        static_cast<int>(message->GetMessageType()));
      stats_.forward_errors->Add(1);
      return Status::InvalidArgument("Invalid message type");
  }

  // Create command.
  const bool is_new_request = true;
  ClientID host = host_id->ToClientId();
  std::unique_ptr<Command> cmd(
    new SerializedSendCommand(std::move(msg),
                              host,
                              is_new_request));

  if (sequence == -1) {
    // Send directly to loop.
    // TODO(pja) 1 : Use other threads based on session.
    return msg_loop_->SendCommand(std::move(cmd), 0);
  } else {
    // Process in order using seqno and session.
    return worker_->EnqueueCommand(session,
                                   sequence,
                                   std::move(cmd),
                                   std::move(host));
  }
}

void Proxy::DestroySession(int64_t session) {
  // TODO (pja) 1 : Handle full worker loop queue.
  worker_->EnqueueRemoveSession(session);
}

Proxy::~Proxy() {
  if (worker_thread_) {
    worker_->Stop();
    env_->WaitForJoin(worker_thread_);
  }
  if (msg_loop_->IsRunning()) {
    msg_loop_->Stop();
    env_->WaitForJoin(msg_thread_);
  }
}

}  // namespace rocketspeed
