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

#ifndef OB_ADMIN_DUMP_BLOCK_H
#define OB_ADMIN_DUMP_BLOCK_H
#include "logservice/palf/log_iterator_impl.h"
#include "logservice/palf/log_reader.h"
#include "logservice/palf/palf_iterator.h"
#include "../ob_admin_log_tool_executor.h"

namespace oceanbase
{
namespace tools
{
class ObGetFileSize
{
public :
  ObGetFileSize(palf::LSN end_lsn)
      : end_lsn_(end_lsn) {}
  palf::LSN operator()() const {return end_lsn_;}
private :
  palf::LSN end_lsn_;
};

class ObAdminDumpBlockHelper {
public:
  int mmap_log_file(char *&buf_out,
                    const int64_t buf_len,
                    const char *path,
                    int &fd_out);
  void unmap_log_file(char *buf_in,
                      const int64_t buf_len,
                      const int64_t fd);

  int get_file_meta(const char *path,
                    palf::LSN &start_lsn,
                    int64_t &header_size,
                    int64_t &body_size);
private:
  int parse_archive_header_(const char *buf_in,
                            const int64_t buf_len,
                            palf::LSN &start_lsn);
  int parse_palf_header_(const char *buf_in,
                         const int64_t buf_len,
                         palf::LSN &start_lsn);
};

class ObAdminDumpBlock
{
public:
  ObAdminDumpBlock(const char *block_path,
                   share::ObAdminMutatorStringArg &str_arg);
  int dump();

private:
  typedef palf::MemPalfGroupBufferIterator ObAdminDumpIterator;

private:
  int dump_();
  int do_dump_(ObAdminDumpIterator &iter, const char *block_namt);
  int parse_single_group_entry_(const palf::LogGroupEntry &entry,
                                const char *block_name,
                                palf::LSN lsn,
                                bool &has_encount_error);
  int parse_single_log_entry_(const palf::LogEntry &entry,
                              const char *block_name,
                              palf::LSN lsn);

#ifdef OB_BUILD_LOG_STORAGE_COMPRESS
  int decompress_();
#endif

private:
  const char *block_path_;
  share::ObAdminMutatorStringArg str_arg_;
};

class ObAdminDumpMetaBlock
{
public:
  ObAdminDumpMetaBlock(const char *block_path, share::ObAdminMutatorStringArg &str_arg);
  int dump();
private:
  typedef palf::MemPalfMetaBufferIterator ObAdminDumpIterator;
private:
  int do_dump_(ObAdminDumpIterator &iter,
               const char *block_name);
private:
  const char *block_path_;
  share::ObAdminMutatorStringArg str_arg_;
};
}
}
#endif
