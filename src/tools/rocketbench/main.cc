// Copyright (c) 2014, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
#define __STDC_FORMAT_MACROS
#include <future>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <assert.h>
#include <gflags/gflags.h>
#include <signal.h>
#include <unistd.h>
#include "include/RocketSpeed.h"
#include "include/Types.h"
#include "include/WakeLock.h"
#include "src/port/port.h"
#include "src/messages/messages.h"
#include "src/messages/msg_loop.h"
#if !defined(OS_ANDROID)
#include "src/test/test_cluster.h"
#endif
#include "src/util/auto_roll_logger.h"
#include "src/util/common/fixed_configuration.h"
#include "src/util/common/guid_generator.h"
#include "src/util/common/host_id.h"
#include "src/util/common/parsing.h"
#include "src/tools/rocketbench/random_distribution.h"
#include "src/client/client.h"
#include "src/util/common/client_env.h"

// This tool can behave as a standalone producer, a standalone
// consumer or both a producer and a consumer.
DEFINE_bool(start_producer, true, "starts the producer");
DEFINE_bool(start_consumer, true, "starts the consumer");
DEFINE_bool(start_local_server, false, "starts an embedded rocketspeed server");
DEFINE_string(storage_url, "", "Storage service URL for local server");

DEFINE_int32(num_threads, 8, "number of threads");
DEFINE_string(config,
              "",
              "Configuration string, if absent uses pilot and copilot lists");
DEFINE_string(pilot_hostnames, "localhost", "hostnames of pilots");
DEFINE_string(copilot_hostnames, "localhost", "hostnames of copilots");
DEFINE_int32(pilot_port, 58600, "port number of pilot");
DEFINE_int32(copilot_port, 58600, "port number of copilot");
DEFINE_uint64(client_workers, 32, "number of client workers");
DEFINE_uint64(cache_size, 0, "size of cache in bytes");
DEFINE_int32(message_size, 100, "message size (bytes)");
DEFINE_uint64(num_topics, 100, "number of topics");
DEFINE_int64(num_messages, 1000, "number of messages to send");
DEFINE_int64(message_rate, 100, "messages per second (0 = unlimited)");
DEFINE_int64(subscribe_rate, 10, "subscribes per second (0 = unlimited)");
DEFINE_int32(idle_timeout, 5, "wait for X seconds until declaring timeout");
DEFINE_bool(await_ack, true, "wait for and include acks in times");
DEFINE_bool(delay_subscribe, false, "start reading after publishing");
DEFINE_bool(logging, true, "enable/disable logging");
DEFINE_bool(report, true, "report results to stdout");
DEFINE_string(namespaceid, rocketspeed::GuestNamespace, "namespace id");
DEFINE_string(topics_distribution, "uniform",
"uniform, normal, poisson, fixed");
DEFINE_int64(topics_mean, 0,
"Mean for Normal and Poisson topic distributions (rounded to nearest int64)");
DEFINE_int64(topics_stddev, 0,
"Standard Deviation for Normal topic distribution (rounded to nearest int64)");
DEFINE_int32(wait_for_debugger, 0, "wait for debugger to attach to me");

using namespace rocketspeed;

rocketspeed::Env* env;
std::shared_ptr<rocketspeed::Logger> info_log;


typedef std::pair<rocketspeed::MsgId, uint64_t> MsgTime;

struct Result {
  bool succeeded;
};

struct ProducerArgs {
  std::vector<std::unique_ptr<rocketspeed::ClientImpl>>* producers;
  rocketspeed::NamespaceID nsid;
  rocketspeed::port::Semaphore* all_ack_messages_received;
  std::atomic<int64_t>* ack_messages_received;
  std::chrono::time_point<std::chrono::steady_clock>* last_ack_message;
  rocketspeed::PublishCallback publish_callback;
  bool result;
};

struct ProducerWorkerArgs {
  int64_t num_messages;
  rocketspeed::NamespaceID namespaceid;
  rocketspeed::Client* producer;
  rocketspeed::PublishCallback publish_callback;
  bool result;
  uint64_t seed;
};

struct ConsumerArgs {
  rocketspeed::port::Semaphore* all_messages_received;
  std::atomic<int64_t>* messages_received;
  std::chrono::time_point<std::chrono::steady_clock>* last_data_message;
  bool result;
};

namespace rocketspeed {

static void ProducerWorker(void* param) {
  ProducerWorkerArgs* args = static_cast<ProducerWorkerArgs*>(param);
  int64_t num_messages = args->num_messages;
  NamespaceID namespaceid = args->namespaceid;
  Client* producer = args->producer;
  PublishCallback publish_callback = args->publish_callback;
  args->result = false;

  // Random number generator.
  std::unique_ptr<rocketspeed::RandomDistributionBase>
    distr(GetDistributionByName(FLAGS_topics_distribution,
                                0,
                                FLAGS_num_topics - 1,
                                static_cast<double>(FLAGS_topics_mean),
                                static_cast<double>(FLAGS_topics_stddev),
                                args->seed));

  // Generate some dummy data.
  std::vector<char> data(FLAGS_message_size);
  const char* data_message = "RocketSpeed ";
  for (int i = 0; i < FLAGS_message_size; ++i) {
    data[i] = data_message[i % strlen(data_message)];
  }
  Slice payload(data.data(), data.size());

  LOG_INFO(info_log, "Starting message loop");
  info_log->Flush();

  // Calculate message rate for this worker.
  int64_t rate = FLAGS_message_rate / FLAGS_num_threads + 1;

  auto start = std::chrono::steady_clock::now();
  for (int64_t i = 0; i < num_messages; ++i) {
    // Create random topic name
    char topic_name[64];
    uint64_t topic_num = distr.get() != nullptr ?
                         distr->generateRandomInt() :
                         i % 100;  // "fixed", 100 messages per topic
    snprintf(topic_name, sizeof(topic_name),
             "benchmark.%llu",
             static_cast<long long unsigned int>(topic_num));

    TopicOptions topic_options;
    // Add ID and timestamp to message ID.
    static std::atomic<uint64_t> message_index;
    uint64_t send_time = env->NowMicros();
    uint64_t index = message_index++;
    snprintf(data.data(), data.size(),
             "%llu %llu",
             static_cast<long long unsigned int>(index),
             static_cast<long long unsigned int>(send_time));

    // Send the message
    PublishStatus ps = producer->Publish(GuestTenant,
                                         topic_name,
                                         namespaceid,
                                         topic_options,
                                         payload,
                                         publish_callback);

    if (!ps.status.ok()) {
      LOG_WARN(info_log,
        "Failed to send message number %llu (%s)",
        static_cast<long long unsigned int>(i),
        ps.status.ToString().c_str());
      info_log->Flush();
      args->result = false;
    }

    if (FLAGS_message_rate) {
      // Check if we are sending messages too fast, and if so, sleep.
      int64_t have_sent = i;

      // Time since we started sending messages.
      auto expired = std::chrono::steady_clock::now() - start;

      // Time that should have expired if we were sending at the desired rate.
      auto expected = std::chrono::microseconds(1000000 * have_sent / rate);

      // If we are ahead of schedule then sleep for the difference.
      if (expected > expired) {
        std::this_thread::sleep_for(expected - expired);
      }
    }
  }
  args->result = true;
}

/*
 ** Produce messages
 */
static void DoProduce(void* params) {

  struct ProducerArgs* args = static_cast<ProducerArgs*>(params);
  std::vector<std::unique_ptr<ClientImpl>>* producers =
    args->producers;
  rocketspeed::NamespaceID namespaceid = args->nsid;
  rocketspeed::port::Semaphore* all_ack_messages_received =
    args->all_ack_messages_received;
  std::atomic<int64_t>* ack_messages_received = args->ack_messages_received;
  std::chrono::time_point<std::chrono::steady_clock>* last_ack_message =
    args->last_ack_message;
  rocketspeed::PublishCallback publish_callback = args->publish_callback;

  // Distribute total number of messages among them.
  std::vector<Env::ThreadId> thread_ids;
  int64_t total_messages = FLAGS_num_messages;
  size_t p = 0;
  ProducerWorkerArgs pargs[1024]; // no more than 1K threads

  for (int32_t remaining = FLAGS_num_threads; remaining; --remaining) {
    int64_t num_messages = total_messages / remaining;
    ProducerWorkerArgs* parg = &pargs[p];
    parg->num_messages = num_messages;
    parg->namespaceid = namespaceid;
    parg->producer = (*producers)[p % producers->size()].get();
    parg->publish_callback = publish_callback;
    parg->seed = p << 32;  // should be consistent between runs.

    thread_ids.push_back(env->StartThread(rocketspeed::ProducerWorker, parg));
    total_messages -= num_messages;
    p++;
    if (p > sizeof(pargs)/sizeof(pargs[0])) {
      break;
    }
  }
  assert(total_messages == 0);
  assert(p == thread_ids.size());

  // Join all the threads to finish production.
  int ret = 0;
  for (unsigned int i = 0; i < thread_ids.size(); ++i) {
    env->WaitForJoin(thread_ids[i]);
    if (!pargs[i].result) {
      if (FLAGS_report) {
        printf("Thread %d failed to send all messages\n", i);
      }
      ret = 1;
    }
  }

  if (FLAGS_await_ack) {
    // Wait for the all_ack_messages_received semaphore to be posted.
    // Keep waiting as long as a message was received in the last 5 seconds.
    auto timeout = std::chrono::seconds(FLAGS_idle_timeout);
    do {
      all_ack_messages_received->TimedWait(timeout);
    } while (ack_messages_received->load() != FLAGS_num_messages &&
             std::chrono::steady_clock::now() - *last_ack_message < timeout);

    ret = ack_messages_received->load() == FLAGS_num_messages ? 0 : 1;
  }
  args->result = ret; // 0: success, 1: error
}

/**
 * Subscribe to topics.
 */
void DoSubscribe(std::vector<std::unique_ptr<ClientImpl>>& consumers,
                 NamespaceID nsid,
                 std::unordered_map<std::string, SequenceNumber> first_seqno) {
  auto start = std::chrono::steady_clock::now();
  size_t c = 0;
  auto rate = FLAGS_subscribe_rate;
  for (uint64_t i = 0; i < FLAGS_num_topics; i++) {
    std::string topic_name("benchmark." + std::to_string(i));
    SequenceNumber seqno = 0;   // start sequence number (0 = only new records)
    if (FLAGS_delay_subscribe) {
      // Find the first seqno published to this topic (or 0 if none published).
      auto it = first_seqno.find(topic_name);
      if (it == first_seqno.end()) {
        seqno = 0;
      } else {
        seqno = it->second;
      }
    }
    consumers[c++ % consumers.size()]->Subscribe(
        GuestTenant, nsid, topic_name, seqno);
    if (rate) {
      int64_t have_sent = i;
      auto expired = std::chrono::steady_clock::now() - start;
      auto expected = std::chrono::microseconds(1000000 * have_sent / rate);
      if (expected > expired) {
        /* sleep override */
        std::this_thread::sleep_for(expected - expired);
      }
    }
  }
}

/*
 ** Receive messages
 */
static void DoConsume(void* param) {
  ConsumerArgs* args = static_cast<ConsumerArgs*>(param);
  rocketspeed::port::Semaphore* all_messages_received =
    args->all_messages_received;
  std::atomic<int64_t>* messages_received = args->messages_received;
  std::chrono::time_point<std::chrono::steady_clock>* last_data_message =
    args->last_data_message;
  // Wait for the all_messages_received semaphore to be posted.
  // Keep waiting as long as a message was received in the last 5 seconds.
  auto timeout = std::chrono::seconds(FLAGS_idle_timeout);
  do {
    all_messages_received->TimedWait(timeout);
  } while (messages_received->load() != FLAGS_num_messages &&
           std::chrono::steady_clock::now() - *last_data_message < timeout);

  args->result = messages_received->load() == FLAGS_num_messages ? 0 : 1;
}

}  // namespace rocketspeed

int main(int argc, char** argv) {
  rocketspeed::Env::InstallSignalHandlers();
  env = rocketspeed::Env::Default();
  GFLAGS::ParseCommandLineFlags(&argc, &argv, true);

  // This loop is needed so that we can attach to this process via
  // the remote gdb debugger on Android systems.
  while (FLAGS_wait_for_debugger) {
    sleep(1);
  }

  // Ignore SIGPIPE, we'll just handle the EPIPE returned by write.
  signal(SIGPIPE, SIG_IGN);

  // Validate flags
  if (FLAGS_num_threads <= 0) {
    fprintf(stderr, "num_threads must be greater than 0.\n");
    return 1;
  }

  if (FLAGS_message_size <= 0 || FLAGS_message_size > 1024*1024) {
    fprintf(stderr, "message_size must be 0-1MB.\n");
    return 1;
  }

  if (FLAGS_num_topics <= 0) {
    fprintf(stderr, "num_topics must be greater than 0.\n");
    return 1;
  }

  if (FLAGS_num_messages <= 0) {
    fprintf(stderr, "num_messages must be greater than 0.\n");
    return 1;
  }
  if (!FLAGS_start_consumer && !FLAGS_start_producer) {
    fprintf(stderr, "You must specify at least one --start_producer "
            "or --start_consumer\n");
    return 1;
  }

  // Create logger
  if (FLAGS_logging) {
    if (!rocketspeed::CreateLoggerFromOptions(rocketspeed::Env::Default(),
                                              "",
                                              "LOG.rocketbench",
                                              0,
                                              0,
#ifdef NDEBUG
                                              rocketspeed::WARN_LEVEL,
#else
                                              rocketspeed::INFO_LEVEL,
#endif
                                              &info_log).ok()) {
      fprintf(stderr, "Error creating logger, aborting.\n");
      return 1;
    }
  } else {
    info_log = std::make_shared<rocketspeed::NullLogger>();
  }

#if defined(OS_ANDROID)
  if (FLAGS_start_local_server) {
    fprintf(stderr, "Servers not supported on Android.\n");
    return 1;
  }
#else
  std::unique_ptr<rocketspeed::LocalTestCluster> test_cluster;
  LocalTestCluster::Options test_options;
  if (FLAGS_start_local_server) {
    test_options.info_log = info_log;
    test_options.start_controltower = true;
    test_options.start_copilot = true;
    test_options.start_pilot = true;
    test_options.storage_url = FLAGS_storage_url;
    if (FLAGS_cache_size) {
      test_options.tower.cache_size = FLAGS_cache_size;
    }
    test_cluster.reset(new rocketspeed::LocalTestCluster(test_options));
  }
#endif

  // Configuration for RocketSpeed.
  std::vector<rocketspeed::HostId> pilots;
  for (auto hostname : rocketspeed::SplitString(FLAGS_pilot_hostnames)) {
    HostId resolved;
    auto st = HostId::Resolve(
        hostname, static_cast<uint16_t>(FLAGS_pilot_port), &resolved);
    if (!st.ok()) {
      LOG_ERROR(info_log, "%s", st.ToString().c_str());
      return 1;
    }
    pilots.emplace_back(std::move(resolved));
  }

  std::vector<rocketspeed::HostId> copilots;
  for (auto hostname : rocketspeed::SplitString(FLAGS_copilot_hostnames)) {
    HostId resolved;
    auto st = HostId::Resolve(
        hostname, static_cast<uint16_t>(FLAGS_copilot_port), &resolved);
    if (!st.ok()) {
      LOG_ERROR(info_log, "%s", st.ToString().c_str());
      return 1;
    }
    copilots.emplace_back(std::move(resolved));
  }

  // Start/end time for benchmark.
  std::chrono::time_point<std::chrono::steady_clock> start, end;

  // Semaphore to signal when all messages have been ack'd
  rocketspeed::port::Semaphore all_ack_messages_received;

  // Time last ack/data message was received.
  std::chrono::time_point<std::chrono::steady_clock> last_ack_message;
  std::chrono::time_point<std::chrono::steady_clock> last_data_message;

  // Benchmark statistics.
  std::mutex all_stats_mutex;
  std::vector<std::unique_ptr<rocketspeed::Statistics>> all_stats;
  rocketspeed::ThreadLocalPtr per_thread_stats;
  rocketspeed::ThreadLocalPtr ack_latency;
  rocketspeed::ThreadLocalPtr recv_latency;

  // Initializes stats for current thread.
  auto InitThreadLocalStats = [&] () {
    if (!per_thread_stats.Get()) {
      std::unique_ptr<rocketspeed::Statistics> stats(
        new rocketspeed::Statistics());
      per_thread_stats.Reset(stats.get());
      ack_latency.Reset(stats->AddLatency("ack-latency"));
      recv_latency.Reset(stats->AddLatency("recv-latency"));
      std::lock_guard<std::mutex> lock(all_stats_mutex);
      all_stats.emplace_back(std::move(stats));
    }
  };

  // Get thread local ack latency histogram.
  auto GetAckLatency = [&] () {
    InitThreadLocalStats();
    return static_cast<rocketspeed::Histogram*>(ack_latency.Get());
  };

  // Get thread local recv latency histogram.
  auto GetRecvLatency = [&] () {
    InitThreadLocalStats();
    return static_cast<rocketspeed::Histogram*>(recv_latency.Get());
  };

  // Create callback for publish acks.
  std::atomic<int64_t> ack_messages_received{0};
  std::atomic<int64_t> failed_publishes{0};

  // Map of topics to the first sequence number in that topic.
  std::unordered_map<std::string, rocketspeed::SequenceNumber> first_seqno;
  std::mutex first_seqno_mutex;

  auto publish_callback =
    [&] (std::unique_ptr<rocketspeed::ResultStatus> rs) {
    uint64_t now = env->NowMicros();

    if (rs->GetStatus().ok()) {
      // Parse message data to get received index.
      rocketspeed::Slice data = rs->GetContents();
      unsigned long long int message_index, send_time;
      std::sscanf(data.data(), "%llu %llu", &message_index, &send_time);
      GetAckLatency()->Record(static_cast<uint64_t>(now - send_time));

      if (FLAGS_delay_subscribe) {
        if (rs->GetStatus().ok()) {
          // Get the min sequence number for this topic to subscribe to later.
          std::string topic = rs->GetTopicName().ToString();
          std::lock_guard<std::mutex> lock(first_seqno_mutex);
          auto it = first_seqno.find(topic);
          if (it == first_seqno.end()) {
            first_seqno[topic] = rs->GetSequenceNumber();
          } else {
            it->second = std::min(it->second, rs->GetSequenceNumber());
          }
        }
      }
    } else {
      ++failed_publishes;
      LOG_WARN(info_log, "Received publish failure response");
    }

    if (FLAGS_await_ack) {
      // This may be the last ack we receive, so set end to the time now.
      end = std::chrono::steady_clock::now();
      last_ack_message = std::chrono::steady_clock::now();

      // If we've received all messages, let the main thread know to finish up.
      if (++ack_messages_received == FLAGS_num_messages) {
        all_ack_messages_received.Post();
      }
    }
  };

  // Semaphore to signal when all data messages have been received
  rocketspeed::port::Semaphore all_messages_received;

  // Create callback for processing messages received
  std::atomic<int64_t> messages_received{0};
  std::vector<bool> is_received(FLAGS_num_messages, false);
  std::mutex is_received_mutex;
  auto receive_callback = [&]
    (std::unique_ptr<rocketspeed::MessageReceived>& rs) {
    uint64_t now = env->NowMicros();
    ++messages_received;
    last_data_message = std::chrono::steady_clock::now();

    // Parse message data to get received index.
    rocketspeed::Slice data = rs->GetContents();
    unsigned long long int message_index, send_time;
    std::sscanf(data.data(), "%llu %llu", &message_index, &send_time);
    if (message_index < static_cast<uint64_t>(FLAGS_num_messages)) {
      LOG_INFO(info_log,
          "Received message %llu with timestamp %llu",
          static_cast<long long unsigned int>(message_index),
          static_cast<long long unsigned int>(send_time));
      GetRecvLatency()->Record(static_cast<uint64_t>(now - send_time));
      std::lock_guard<std::mutex> lock(is_received_mutex);
      if (is_received[message_index]) {
        LOG_WARN(info_log,
          "Received message %llu twice.",
          static_cast<long long unsigned int>(message_index));
      }
      is_received[message_index] = true;
    } else {
      LOG_WARN(info_log,
          "Received out of bounds message index (%llu), message was (%s)",
          static_cast<long long unsigned int>(message_index),
          data.data());
    }

    // If we've received all messages, let the main thread know to finish up.
    if (messages_received.load() == FLAGS_num_messages) {
      all_messages_received.Post();
    }
  };

  // Subscribe callback.
  std::atomic<uint64_t> num_topics_subscribed{0};
  auto subscribe_callback = [&](const rocketspeed::SubscriptionStatus& ss) {
    if (!ss.IsSubscribed()) {
      LOG_WARN(info_log, "Received an unsubscribe response");
    }
  };

  // Data loss callback.
  auto data_loss_callback = [&](std::unique_ptr<DataLossInfo>& msg) {
    LOG_ERROR(info_log, "Data loss has been detected.");
  };

  std::vector<std::unique_ptr<rocketspeed::ClientImpl>> clients;
  for (size_t i = 0; i < FLAGS_client_workers; ++i) {
    rocketspeed::ClientOptions options;
    options.info_log = info_log;
    options.num_workers = 1;

    if (!FLAGS_config.empty()) {
      // Use provided configuration string.
      auto st = rocketspeed::Configuration::CreateConfiguration(
          info_log, FLAGS_config, &options.config);
      if (!st.ok()) {
        LOG_FATAL(info_log,
                  "Failed to parse configuration: %s",
                  st.ToString().c_str());
        return 1;
      }
    } else {
      // Fall back to picking pilot and copilot in a round robin fashion.
      options.config = std::make_shared<rocketspeed::FixedConfiguration>(
          pilots[i % pilots.size()], copilots[i % copilots.size()]);
    }

    std::unique_ptr<rocketspeed::ClientImpl> client;
    // Create the client.
    auto st = rocketspeed::ClientImpl::Create(std::move(options), &client);
    if (!st.ok()) {
      LOG_ERROR(info_log, "Failed to open client: %s.", st.ToString().c_str());
      return 1;
    }
    client->SetDefaultCallbacks(subscribe_callback,
                                receive_callback,
                                data_loss_callback);
    clients.emplace_back(client.release());
  }
  rocketspeed::NamespaceID nsid =
    static_cast<rocketspeed::NamespaceID>(FLAGS_namespaceid);

  // Subscribe to topics (don't count this as part of the time)
  // Also waits for the subscription responses.
  if (!FLAGS_delay_subscribe) {
    if (FLAGS_start_consumer) {
      printf("Subscribing to topics... ");
      fflush(stdout);
      DoSubscribe(clients, nsid, std::move(first_seqno));
      env->SleepForMicroseconds(1000000);  // allow 1 seconds for subscribe
      printf("done\n");
    }

    // Start the clock.
    start = std::chrono::steady_clock::now();
  }

  ProducerArgs pargs;
  ConsumerArgs cargs;
  rocketspeed::Env::ThreadId producer_threadid = 0;
  rocketspeed::Env::ThreadId consumer_threadid = 0;

  // Start producing messages
  if (FLAGS_start_producer) {
    printf("Publishing messages.\n");
    fflush(stdout);
    pargs.producers = &clients;
    pargs.nsid = nsid;
    pargs.all_ack_messages_received = &all_ack_messages_received;
    pargs.ack_messages_received = &ack_messages_received;
    pargs.last_ack_message = &last_ack_message;
    pargs.publish_callback = publish_callback;
    producer_threadid = env->StartThread(rocketspeed::DoProduce,
                                         static_cast<void*>(&pargs),
                                         "ProducerMain");
  }

  // If we are not 'delayed', then we are already subscribed to
  // topics, simply starts threads to consume
  if (FLAGS_start_consumer && !FLAGS_delay_subscribe) {
    printf("Waiting for messages.\n");
    fflush(stdout);
    cargs.all_messages_received = &all_messages_received;
    cargs.messages_received = &messages_received;
    cargs.last_data_message = &last_data_message;
    consumer_threadid = env->StartThread(rocketspeed::DoConsume,
                                         &cargs,
                                         "ConsumerMain");
  }

  // Wait for all producers to finish
  int ret = 0;
  if (FLAGS_start_producer) {
    env->WaitForJoin(producer_threadid);
    ret = pargs.result;
    if (ack_messages_received.load() != FLAGS_num_messages) {
      printf("Time out awaiting publish acks.\n");
      fflush(stdout);
      ret = 1;
    } else {
      printf("All messages published.\n");
      fflush(stdout);
    }
  }

  // If we are delayed, then start subscriptions after all
  // publishers are completed.
  uint64_t subscribe_time = 0;
  if (FLAGS_delay_subscribe) {
    assert(FLAGS_start_consumer);
    printf("Subscribing (delayed) to topics.\n");
    fflush(stdout);

    // Start the clock.
    start = std::chrono::steady_clock::now();

    // Subscribe to topics
    subscribe_time = env->NowMicros();
    DoSubscribe(clients, nsid, std::move(first_seqno));
    subscribe_time = env->NowMicros() - subscribe_time;
    printf("Took %" PRIu64 "ms to subscribe to %" PRIu64 " topics",
      subscribe_time / 1000,
      FLAGS_num_topics);

    // Wait for all messages to be received
    printf("Waiting (delayed) for messages.\n");
    fflush(stdout);
    cargs.all_messages_received = &all_messages_received;
    cargs.messages_received = &messages_received;
    cargs.last_data_message = &last_data_message;
    consumer_threadid = env->StartThread(rocketspeed::DoConsume,
                                         &cargs,
                                         "ConsumerMain");
  }

  if (FLAGS_start_consumer) {
    // Wait for Consumer thread to exit
    env->WaitForJoin(consumer_threadid);
    ret = cargs.result;
    if (messages_received.load() != FLAGS_num_messages) {
      printf("Time out awaiting messages.\n");
      fflush(stdout);
    } else {
      printf("All messages received.\n");
      fflush(stdout);
    }
  }

  end = std::chrono::steady_clock::now();

  // Calculate total time.
  auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
    end - start);

  if (FLAGS_report) {
    uint32_t total_ms = static_cast<uint32_t>(total_time.count());
    if (total_ms == 0) {
      // To avoid divide-by-zero on near-instant benchmarks.
      total_ms = 1;
    }

    if (FLAGS_delay_subscribe) {
      // Check that subscribe time wasn't a significant portion of total time.
      uint32_t subscribe_ms = static_cast<uint32_t>(subscribe_time / 1000);
      double subscribe_pct = double(subscribe_ms) / double(total_ms);
      if (subscribe_pct > 0.01) {
        printf(
          "\n"
          "WARNING: Time waiting for subscription was %.2lf%% of total time.\n"
          "         Consider subscribing to fewer topics.\n",
          100.0 * subscribe_pct);
      }
    }

    uint64_t msg_per_sec = 1000 *
                           FLAGS_num_messages /
                           total_ms;
    uint64_t bytes_per_sec = 1000 *
                             FLAGS_num_messages *
                             FLAGS_message_size /
                             total_ms;

    printf("\n");
    printf("Results\n");
    printf("%lld messages sent\n",
           static_cast<long long unsigned int>(FLAGS_num_messages));
    printf("%lld messages sends acked\n",
           static_cast<long long unsigned int>(ack_messages_received.load()));
    if (failed_publishes != 0) {
      printf("%lld published failed\n",
             static_cast<long long unsigned int>(failed_publishes.load()));
    }
    if (FLAGS_start_consumer) {
      printf("%lld messages received\n",
             static_cast<long long unsigned int>(messages_received.load()));
    }

    if (FLAGS_start_consumer &&
        messages_received.load() != FLAGS_num_messages) {
      // Print out dropped messages if there are any. This helps when
      // debugging problems.
      printf("\n");
      printf("Messages failed to receive\n");

      for (uint64_t i = 0; i < is_received.size(); ++i) {
        if (!is_received[i]) {
          // Find the range of message IDs dropped (e.g. 100-200)
          uint64_t j;
          for (j = i; j < is_received.size(); ++j) {
            if (is_received[j]) {
              break;
            }
          }
          // Print the dropped messages, either individal (e.g. 100) or
          // as a range (e.g. 100-200)
          if (i == j - 1) {
            printf("%llu\n", static_cast<long long unsigned int>(i));
          } else {
            printf("%llu-%llu\n", static_cast<long long unsigned int>(i),
                   static_cast<long long unsigned int>(j - 1));
          }
          i = j;
        }
      }
    }

    // Only report results if everything succeeded.
    // Otherwise, they don't make sense.
    if (ret == 0) {
      printf("\n");
      printf("Throughput\n");
      printf("%" PRIu64 " messages/s\n", msg_per_sec);
      printf("%.2lf MB/s\n", static_cast<double>(bytes_per_sec) * 1e-6);

      rocketspeed::Statistics stats;

      // Aggregate per-thread stats.
      std::lock_guard<std::mutex> lock(all_stats_mutex);
      for (auto& s : all_stats) {
        stats.Aggregate(s->MoveThread());
      }

#if !defined(OS_ANDROID)
      if (FLAGS_start_local_server) {
        stats.Aggregate(test_cluster->GetStatisticsSync());
      }
#endif
      for (auto& client : clients) {
        stats.Aggregate(client->GetStatisticsSync());
      }

      printf("\n");
      printf("Statistics\n");
      printf("%s", stats.Report().c_str());
    }
  }
  fflush(stdout);

  return ret;
}
