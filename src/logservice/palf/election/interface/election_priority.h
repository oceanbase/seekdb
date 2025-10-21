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

#ifndef LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_PRIORITY_H
#define LOGSERVICE_PALF_ELECTION_INTERFACE_OB_I_ELECTION_PRIORITY_H

#include "lib/ob_define.h"
#include "lib/guard/ob_unique_guard.h"
#include "lib/string/ob_string_holder.h"

namespace oceanbase
{
namespace palf
{
namespace election
{

class ElectionPriority
{
public:
  virtual ~ElectionPriority() {}
  // Print the capability to log priority
  virtual int64_t to_string(char *buf, const int64_t buf_len) const = 0;
  // Priority needs serialization capability to be passed to other replicas via messages
  virtual int serialize(char* buf, const int64_t buf_len, int64_t& pos) const = 0;
  virtual int deserialize(const char* buf, const int64_t data_len, int64_t& pos) = 0;
  virtual int64_t get_serialize_size(void) const = 0;
  // The method for actively refreshing election priority
  virtual int refresh() = 0;
  // The method for comparing between priorities
  virtual int compare_with(const ElectionPriority &rhs,
                           const uint64_t compare_version,
                           const bool decentralized_voting,
                           int &result,
                           common::ObStringHolder &reason) const = 0;
  virtual int get_size_of_impl_type() const = 0;
  virtual void placement_new_impl(void *ptr) const = 0;
  // Skip RCS and directly switch to leader
  virtual bool has_fatal_failure() const = 0;
};

}
}
}
#endif
