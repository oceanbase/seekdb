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

#ifndef OCEANBASE_LOG_MINER_UNITTEST_UTILS_H_
#define OCEANBASE_LOG_MINER_UNITTEST_UTILS_H_

#include "ob_log_miner_br.h"
#include "rpc/obmysql/ob_mysql_global.h"
#include "gtest/gtest.h"
#define private public
#include "ob_log_miner_record.h"
#undef private
namespace oceanbase
{
namespace oblogminer
{
ObLogMinerBR *build_logminer_br(binlogBuf *new_bufs,
              binlogBuf *old_bufs,
              RecordType type,
              lib::Worker::CompatMode compat_mode,
              const char *db_name,
              const char *table_name,
              const int arg_count, ...);

ObLogMinerBR *build_logminer_br(binlogBuf *new_bufs,
              binlogBuf *old_bufs,
              RecordType type,
              lib::Worker::CompatMode compat_mode,
              const char *db_name,
              const char *table_name,
              const char *encoding,
              const int arg_count, ...);

void destroy_miner_br(ObLogMinerBR *&br);

ObLogMinerRecord *build_logminer_record(ObIAllocator &alloc,
                  lib::Worker::CompatMode	compat_mode,
                  uint64_t tenant_id,
                  int64_t orig_cluster_id,
                  const char *tenant_name,
                  const char *database_name,
                  const char *tbl_name,
                  int64_t trans_id,
                  const char* const * pks,
                  const int64_t pk_cnt,
                  const char* const * uks,
                  const int64_t uk_cnt,
                  const char* row_unique_id,
                  RecordType record_type,
                  int64_t commit_ts_us,
                  const char * redo_stmt,
                  const char * undo_stmt);

void destroy_miner_record(ObLogMinerRecord *&rec);

}
}

#endif
