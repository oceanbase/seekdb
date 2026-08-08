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

#include "share/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/container/ob_bit_set.h"
#include "lib/string/ob_sql_string.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/lock/ob_thread_cond.h"
#include "common/object/ob_object.h"
#include "sql/resolver/cmd/ob_load_data_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "query/engine/cmd/ob_load_data_stat.h"

#ifndef OCEANBASE_SQL_ENGINE_CMD_LOAD_DATA_UTILS_H_
#define OCEANBASE_SQL_ENGINE_CMD_LOAD_DATA_UTILS_H_
namespace oceanbase
{
namespace sql {

enum class ObLoadDupActionType;
class ObSQLSessionInfo;

static const int64_t DEFAULT_BUFFERRED_ROW_COUNT = 100; // must be less than 2^15
static const int64_t DEFAULT_PARALLEL_THREAD_COUNT = 4;

class ObLoadDataUtils {
public:

  static const char NULL_VALUE_FLAG;

  static int build_insert_sql_string_head(ObLoadDupActionType insert_mode,
                                          const common::ObString &table_name,
                                          const common::ObIArray<common::ObString> &insert_keys,
                                          common::ObSqlString &insertsql_keys,
                                          bool need_gather_opt_stat = false);

  static int check_need_opt_stat_gather(ObExecContext &ctx,
                                        ObLoadDataStmt &load_stmt,
                                        bool &need_opt_stat_gather);

  static int check_session_status(ObSQLSessionInfo &session, int64_t reserved_us = 0);
};

// Local LOAD DATA workers use this controller to bound in-process tasks.  It
// contains no transport or server-routing state.
class ObParallelTaskController
{
public:
  ObParallelTaskController() : max_parallelism_(0), task_cnt_(0), processing_cnt_(0) {}
  ~ObParallelTaskController() {}
  int init(int64_t max_parallelism);
  int on_next_task();
  int on_task_finished();
  int64_t get_next_task_id() { return task_cnt_++; }
  void wait_all_task_finish(const char *task_name = NULL, int64_t until_ts = INT64_MAX);
  int64_t get_processing_task_cnt() { return ATOMIC_LOAD(&processing_cnt_); }
  int64_t get_total_task_cnt() { return task_cnt_; }
  int64_t get_max_parallelism() { return max_parallelism_; }
private:
  int64_t max_parallelism_;
  int64_t task_cnt_;
  volatile int64_t processing_cnt_;
  common::ObThreadCond vacant_cond_;
};

template <typename T>
class ObConcurrentFixedCircularArray
{
public:
  ObConcurrentFixedCircularArray()
    : array_size_(0), data_(NULL), head_pos_(0), tail_pos_(0),
      lock_(common::ObLatchIds::LOAD_DATA_RPC_CB_LOCK)
  {}
  ~ObConcurrentFixedCircularArray()
  {
    if (NULL != data_) {
      ob_free_align(const_cast<T *>(data_));
    }
  }
  int init(int64_t array_size)
  {
    int ret = common::OB_SUCCESS;
    if (OB_UNLIKELY(array_size <= 0)) {
      ret = common::OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(data_ = static_cast<T *>(ob_malloc_align(
                 CACHE_ALIGN_SIZE, array_size * sizeof(T), "LoadData")))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
      LIB_LOG(WARN, "alloc memory failed", K(ret));
    } else {
      array_size_ = array_size;
    }
    return ret;
  }
  OB_INLINE int push_back(const T &obj)
  {
    common::ObSpinLockGuard guard(lock_);
    int ret = common::OB_SUCCESS;
    int64_t pos = ATOMIC_FAA(&head_pos_, 1);
    if (OB_UNLIKELY(pos - ATOMIC_LOAD(&tail_pos_) >= array_size_)) {
      ret = common::OB_SIZE_OVERFLOW;
    } else {
      ATOMIC_STORE(&data_[pos % array_size_], obj);
    }
    return ret;
  }
  OB_INLINE int pop(T &output)
  {
    common::ObSpinLockGuard guard(lock_);
    int ret = common::OB_SUCCESS;
    int64_t pos = ATOMIC_FAA(&tail_pos_, 1);
    if (OB_UNLIKELY(pos >= ATOMIC_LOAD(&head_pos_))) {
      ret = common::OB_ARRAY_OUT_OF_RANGE;
    } else {
      output = ATOMIC_SET(&data_[pos % array_size_], NULL);
    }
    return ret;
  }
  OB_INLINE int64_t count()
  {
    return ATOMIC_LOAD(&head_pos_) - ATOMIC_LOAD(&tail_pos_);
  }
private:
  int64_t array_size_;
  volatile T *data_;
  volatile int64_t head_pos_;
  volatile int64_t tail_pos_;
  common::ObSpinLock lock_;
};

struct ObLoadDataGID
{
  static volatile int64_t GlobalLoadDataID;
  static void generate_new_id(ObLoadDataGID &gid)
  {
    gid.id = ATOMIC_AAF(&GlobalLoadDataID, 1);
  }
  ObLoadDataGID() : id(-1) {}
  void reset() { id = -1; }
  bool is_valid() const { return id  > 0; }
  uint64_t hash() const { return common::murmurhash(&id, sizeof(id), 0); }
  int hash(uint64_t &hash_val) const { hash_val = hash(); return OB_SUCCESS; }
  bool operator==(const ObLoadDataGID &other) const { return id == other.id; }
  void operator=(const ObLoadDataGID &other) { id = other.id; }
  int64_t id;
  TO_STRING_KV(K(id));
};


class ObGetAllJobStatusOp
{
public:
  ObGetAllJobStatusOp();
  ~ObGetAllJobStatusOp();

public:
  void reset();
  int operator()(common::hash::HashMapPair<ObLoadDataGID, ObLoadDataStat*> &entry);
  int get_next_job_status(ObLoadDataStat *&job_status);

private:
  common::ObSEArray<ObLoadDataStat *, 10> job_status_array_;
  int32_t current_job_index_;
};

class ObLoadDataStatGuard
{
public:
  ObLoadDataStatGuard() : stat_(nullptr) {}
  ObLoadDataStatGuard(const ObLoadDataStatGuard &rhs) : stat_(nullptr)
  {
    aquire(rhs.stat_);
  }
  ~ObLoadDataStatGuard()
  {
    release();
  }

  void aquire(ObLoadDataStat *stat)
  {
    release();
    stat_ = stat;
    if (nullptr != stat_) {
      stat_->aquire();
    }
  }

  void release()
  {
    if (nullptr != stat_) {
      stat_->release();
      stat_ = nullptr;
    }
  }

  ObLoadDataStat *get() const { return stat_; }

  // ObLoadDataStat *operator->() { return stat_; }
  // const ObLoadDataStat *operator->() const { return stat_; }

  ObLoadDataStatGuard &operator=(const ObLoadDataStatGuard &rhs)
  {
    aquire(rhs.stat_);
    return *this;
  }

  TO_STRING_KV(KPC_(stat));

private:
  ObLoadDataStat *stat_;
};

class ObGlobalLoadDataStatMap
{
public:
  static ObGlobalLoadDataStatMap *getInstance();
  ObGlobalLoadDataStatMap() : is_inited_(false) {}
  int init();
  int register_job(const ObLoadDataGID &id, ObLoadDataStat *job_status);
  int unregister_job(const ObLoadDataGID &id, ObLoadDataStat *&job_status);
  int get_job_status(const ObLoadDataGID &id, ObLoadDataStat *&job_status);
  int get_all_job_status(ObGetAllJobStatusOp &job_status_op);
private:
  typedef common::hash::ObHashMap<ObLoadDataGID, ObLoadDataStat*,
          common::hash::SpinReadWriteDefendMode> HASH_MAP;
  static const int64_t bucket_num = 1000;
  static ObGlobalLoadDataStatMap *instance_;
  HASH_MAP map_;
  bool is_inited_;
};

}
}


#endif // OCEANBASE_SQL_ENGINE_CMD_LOAD_DATA_UTILS_H_
