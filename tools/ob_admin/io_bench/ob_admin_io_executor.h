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

#ifndef OB_ADMIN_IO_EXECUTOR_H_
#define OB_ADMIN_IO_EXECUTOR_H_
#include "../ob_admin_executor.h"

namespace oceanbase
{
namespace tools
{

class ObAdminIOExecutor : public ObAdminExecutor
{
public:
  ObAdminIOExecutor();
  virtual ~ObAdminIOExecutor();
  virtual int execute(int argc, char *argv[]);
  void reset();
private:
  static const int64_t DEFAULT_BENCH_FILE_SIZE = 1024L * 1024L * 1024L * 100L;
  int parse_cmd(int argc, char *argv[]);
  void print_usage();
  const char *conf_dir_;
  const char *data_dir_;
  const char *file_size_;
};

}
}

#endif /* OB_ADMIN_IO_EXECUTOR_H_ */
