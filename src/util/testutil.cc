//  Copyright (c) 2014, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
#include "src/util/testutil.h"

#include "src/util/random.h"

namespace rocketspeed {
namespace test {

Slice RandomString(Random* rnd, int len, std::string* dst) {
  dst->resize(len);
  for (int i = 0; i < len; i++) {
    (*dst)[i] = static_cast<char>(' ' + rnd->Uniform(95));   // ' ' .. '~'
  }
  return Slice(*dst);
}

std::string RandomKey(Random* rnd, int len) {
  // Make sure to generate a wide variety of characters so we
  // test the boundary conditions for short-key optimizations.
  static const char kTestChars[] = {
    '\0', '\1', 'a', 'b', 'c', 'd', 'e', '\xfd', '\xfe', '\xff'
  };
  std::string result;
  for (int i = 0; i < len; i++) {
    result += kTestChars[rnd->Uniform(sizeof(kTestChars))];
  }
  return result;
}


extern Slice CompressibleString(Random* rnd, double compressed_fraction,
                                int len, std::string* dst) {
  int raw = static_cast<int>(len * compressed_fraction);
  if (raw < 1) raw = 1;
  std::string raw_data;
  RandomString(rnd, raw, &raw_data);

  // Duplicate the random data until we have filled "len" bytes
  dst->clear();
  while (dst->size() < (unsigned int)len) {
    dst->append(raw_data);
  }
  dst->resize(len);
  return Slice(*dst);
}

}  // namespace test
}  // namespace rocketspeed