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

#ifndef OB_ADMIN_UNCOMPRESS_PLAN_EXECUTOR_H_
#define OB_ADMIN_UNCOMPRESS_PLAN_EXECUTOR_H_

#include "../ob_admin_executor.h"

namespace oceanbase
{
namespace tools
{

class ObAdminUncompressPlanExecutor : public ObAdminExecutor
{
public:
  ObAdminUncompressPlanExecutor() {};
  virtual ~ObAdminUncompressPlanExecutor() {};
  int execute(int argc, char *argv[]) override final;
private:
  int uncompress_plan();
  int get_compressed_plan(ObArenaAllocator &allocator, const std::string &hex_str,
                          char *&compressed_str);
  int parse_cmd(int argc, char *argv[]);
  void print_usage();
private:
  int64_t uncompress_len_{0};
  int64_t compressed_len_{0};
  std::string plan_str_;
};

} //namespace tools
} //namespace oceanbase

#endif /* OB_ADMIN_UNCOMPRESS_PLAN_EXECUTOR_H_ */
