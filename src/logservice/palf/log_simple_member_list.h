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

#ifndef OCEANBASE_LOGSERVICE_LOG_SIMPLE_MEMBER_LIST_H_
#define OCEANBASE_LOGSERVICE_LOG_SIMPLE_MEMBER_LIST_H_

#include "common/ob_member_list.h"
#include "lib/container/ob_se_array.h"
#include "lib/net/ob_addr.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace palf
{
class LogSimpleMemberList final
{
public:
  LogSimpleMemberList() { reset(); }
public:
  int add_server(const common::ObAddr &server);
  bool contains(const common::ObAddr &server) const;
  int64_t get_count() const { return ATOMIC_LOAD(&count_); }
  void reset();
  int64_t to_string(char *buf, const int64_t buf_len) const;
  int deep_copy(const common::ObMemberList &member_list);
  int deep_copy_to(common::ObMemberList &member_list);
  int remove_server(const common::ObAddr &server);
private:
  int8_t count_;
  common::ObAddr server_[common::OB_MAX_MEMBER_NUMBER];
};

typedef common::ObSEArray<LogSimpleMemberList, 16> LogSimpleMemberListArray;

class LogAckList
{
public:
  LogAckList() { reset(); }
  ~LogAckList() {}
public:
  int add_server(const common::ObAddr &server);
  int64_t get_count() const;
  void reset();
  int64_t to_string(char *buf, const int64_t buf_len) const;
private:
  const static int64_t ACK_LIST_SERVER_NUM = common::OB_MAX_MEMBER_NUMBER / 2;
  common::ObAddr server_[ACK_LIST_SERVER_NUM];
};
} // namespace palf
} // namespace oceanbase

#endif // OCEANBASE_CLOG_OB_SIMPLE_MEMBER_LIST_H_
