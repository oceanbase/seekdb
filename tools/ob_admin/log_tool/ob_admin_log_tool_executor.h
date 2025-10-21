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

#ifndef OB_ADMIN_LOG_EXECUTOR_H_
#define OB_ADMIN_LOG_EXECUTOR_H_
#include "../ob_admin_executor.h"
#include "share/ob_admin_dump_helper.h"
namespace oceanbase
{
namespace tools
{
class ObAdminLogExecutor : public ObAdminExecutor
{
public:
  ObAdminLogExecutor() : mutator_str_buf_(nullptr), mutator_buf_size_(0){}
  virtual ~ObAdminLogExecutor();
  virtual int execute(int argc, char *argv[]);

private:
  void print_usage();
  int dump_log(int argc, char **argv);
  int decompress_log(int argc, char **argv);
  int dump_meta(int argc, char **argv);
  int dump_tx_format(int argc, char **argv);
  int dump_filter(int argc, char **argv);
  int stat(int argc, char **argv);
  int parse_options(int argc, char *argv[]);
  int dump_all_blocks_(int argc, char **argv, share::LogFormatFlag flag);
  int dump_single_block_(const char *block_path,
                         share::ObAdminMutatorStringArg &str_arg);
  int dump_single_meta_block_(const char *block_path,
                              share::ObAdminMutatorStringArg &str_arg);
  int dump_single_log_block_(const char *block_path,
                             share::ObAdminMutatorStringArg &str_arg);
  int alloc_mutator_string_buf_();
  int concat_file_(const char *first_path, const char *second_path);
private:
  const static int64_t MAX_TX_LOG_STRING_SIZE = 5*1024*1024;
  const static int64_t MAX_DECOMPRESSED_BUF_SIZE = palf::MAX_LOG_BODY_SIZE;


  char *mutator_str_buf_;
  int64_t mutator_buf_size_;
  char *decompress_buf_;
  int64_t decompress_buf_size_;
  share::ObAdminLogDumpFilter filter_;
};
}
}
#endif
