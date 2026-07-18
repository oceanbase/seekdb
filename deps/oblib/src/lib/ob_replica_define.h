/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef OB_LIB_REPLICA_DEFINE_H_
#define OB_LIB_REPLICA_DEFINE_H_

#include <stdint.h>
#include <stdlib.h>

#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace common
{

class ObReplicaProperty
{
  OB_UNIS_VERSION(1);
public:
  ObReplicaProperty() : memstore_percent_(100), reserved_(0) {}

  static ObReplicaProperty create_property(int64_t memstore_percent)
  {
    ObReplicaProperty tmp;
    tmp.memstore_percent_ = memstore_percent & 0x7f;

    return tmp;
  }

  int set_memstore_percent(int64_t memstore_percent);
  int64_t get_memstore_percent() const { return memstore_percent_; }

  bool is_valid() const
  {
    return memstore_percent_ <= 100;
  }

  void reset()
  {
    memstore_percent_ = 100;
  }

  bool operator ==(const ObReplicaProperty &o) const { return property_ == o.property_; }

  TO_STRING_KV(K(memstore_percent_));

private:
  union {
    struct {
      uint64_t memstore_percent_ : 7; // 0-100
      uint64_t reserved_ : 57;
    };
    uint64_t property_;
  };
};

} // common
} // oceanbase

#endif /* OB_LIB_REPLICA_DEFINE_H_ */
