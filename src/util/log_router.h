// Copyright (c) 2014, Facebook, Inc. All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#pragma once

#include "include/Types.h"
#include "src/util/storage.h"

namespace rocketspeed {

/**
 * Class that provides logic for routing topic names to logs.
 * This will primarily be used by the Pilots and Control Towers when sending
 * messages to, and receiving message from the log storage.
 *
 * The topic to log mapping uses jump consistent hashing, which is a fast
 * low memory way of mapping keys to an arbitrary number of buckets in a way
 * that shuffles the mapping minimally when the number of buckets increases.
 */
class LogRouter {
 public:
  /**
   * Constructs a new LogRouter.
   * All clients should use either the same LogRouter instance, or different
   * LogRouter instances created with identical parameters. Otherwise, topics
   * will not map to the same logs for different instances.
   *
   * @param numLogs The maximum number of different log IDs to use.
   */
  explicit LogRouter(uint64_t numLogs);

  /**
   * Gets the Log ID where a topic's messages are to be stored.
   *
   * @param topic A topic to lookup.
   * @param out Where to place the resulting Log ID.
   * @return on success OK(), otherwise errorcode.
   */
  Status GetLogID(const Topic& topic, LogID* out) const;

 private:
  /**
   * Assigns the key into a bucket index (< buckets) in a well distributed
   * manner, and also minimally changes the assigned bucket of a key when the
   * number of buckets changes.
   */
  static uint64_t JumpConsistentHash(uint64_t key, uint64_t buckets);

  uint64_t _numLogs;
};

}  // namespace rocketspeed