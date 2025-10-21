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

#ifndef OB_ADMIN_PARSER_GROUP_ENTRY_H_
#define OB_ADMIN_PARSER_GROUP_ENTRY_H_
#include "logservice/palf/log_group_entry.h"
#include "share/ob_admin_dump_helper.h"
namespace oceanbase
{
namespace palf
{
class LogEntry;
}
namespace tools
{
class ObAdminParserGroupEntry
{
public:
  ObAdminParserGroupEntry(const char *buf, const int64_t buf_len,
                          share::ObAdminMutatorStringArg &str_arg);
  int get_next_log_entry(palf::LogEntry &log_entry);
private:
  int do_parse_one_log_entry_(palf::LogEntry &log_entry);
private:
  const char *buf_;
  int64_t curr_pos_;
  int64_t end_pos_;
  share::ObAdminMutatorStringArg str_arg_;
};
}
}
#endif
