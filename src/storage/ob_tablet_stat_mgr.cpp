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

#define USING_LOG_PREFIX STORAGE

#include "ob_tablet_stat_mgr.h"
#include "lib/alloc/alloc_func.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "storage/access/ob_global_iterator_pool.h"
#include "storage/compaction/ob_partition_merge_policy.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;


bool ObTransNodeDMLStat::empty() const
{
  return 0 >= insert_row_count_ &&
         0 >= update_row_count_ &&
         0 >= delete_row_count_;
}

void ObTransNodeDMLStat::atomic_inc(const ObTransNodeDMLStat &other)
{
  if (other.empty()) {
    // do nothing
  } else {
    (void) ATOMIC_AAFx(&insert_row_count_, other.insert_row_count_, 0/*placeholder*/);
    (void) ATOMIC_AAFx(&update_row_count_, other.update_row_count_, 0/*placeholder*/);
    (void) ATOMIC_AAFx(&delete_row_count_, other.delete_row_count_, 0/*placeholder*/);
    (void) ATOMIC_AAFx(&snapshot_gc_scn_row_count_,
        other.snapshot_gc_scn_row_count_, 0/*placeholder*/);
  }
}


/************************************* ObTabletStatKey *************************************/
ObTabletStatKey::ObTabletStatKey(
  const uint64_t tablet_id)
  : tablet_id_(tablet_id)
{
}

ObTabletStatKey::ObTabletStatKey(
  const ObTabletID tablet_id)
  : tablet_id_(tablet_id)
{
}

ObTabletStatKey::~ObTabletStatKey()
{
}

void ObTabletStatKey::reset()
{
  tablet_id_.reset();
}

uint64_t ObTabletStatKey::hash() const
{
  uint64_t hash_val = 0;
  hash_val = tablet_id_.hash();
  return hash_val;
}

int ObTabletStatKey::hash(uint64_t &hash_val) const
{
  hash_val = hash();
  return OB_SUCCESS;
}

bool ObTabletStatKey::is_valid() const
{
  return tablet_id_.is_valid();
}

bool ObTabletStatKey::operator==(const ObTabletStatKey &other) const
{
  bool bret = true;
  if (this == &other) {
  } else if (tablet_id_ != other.tablet_id_) {
    bret = false;
  }
  return bret;
}



/************************************* ObTabletStat *************************************/
bool ObTabletStat::is_valid() const
{
  return tablet_id_ > 0;
}

bool ObTabletStat::check_need_report() const
{
  bool bret = false;
  ObTabletID tablet_id(tablet_id_);

  if (tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (0 < merge_cnt_) { // report by compaction
    bret = get_total_merge_row_count() >= MERGE_REPORT_MIN_ROW_CNT;
  } else if (0 < query_cnt_) { // only report the slow query
    const int64_t boost_factor = tablet_id.is_inner_tablet() ? 2 : 1;
    if (scan_physical_row_cnt_ > 0 &&
        scan_physical_row_cnt_ >= scan_logical_row_cnt_ * QUERY_REPORT_INEFFICIENT_THRESHOLD * boost_factor) {
      bret = true;
    }

    if (!bret && scan_micro_block_cnt_ > 0 &&
        scan_micro_block_cnt_ >= pushdown_micro_block_cnt_ * QUERY_REPORT_INEFFICIENT_THRESHOLD * boost_factor) {
      bret = true;
    }

    if (!bret && exist_row_total_table_cnt_ > 0 &&
        exist_row_total_table_cnt_ >= exist_row_read_table_cnt_ * QUERY_REPORT_INEFFICIENT_THRESHOLD * boost_factor) {
      bret = true;
    }
  }
  return bret;
}

ObTabletStat& ObTabletStat::operator=(const ObTabletStat &other)
{
  if (this != &other) {
    MEMCPY(this, &other, sizeof(ObTabletStat));
  }
  return *this;
}

ObTabletStat& ObTabletStat::operator+=(const ObTabletStat &other)
{
  if (other.is_valid()) {
    tablet_id_ = other.tablet_id_;
    query_cnt_ += other.query_cnt_;
    merge_cnt_ += other.merge_cnt_;
    scan_logical_row_cnt_ += other.scan_logical_row_cnt_;
    scan_physical_row_cnt_ += other.scan_physical_row_cnt_;
    scan_micro_block_cnt_ += other.scan_micro_block_cnt_;
    pushdown_micro_block_cnt_ += other.pushdown_micro_block_cnt_;
    exist_row_total_table_cnt_ += other.exist_row_total_table_cnt_;
    exist_row_read_table_cnt_ += other.exist_row_read_table_cnt_;
    insert_row_cnt_ += other.insert_row_cnt_;
    update_row_cnt_ += other.update_row_cnt_;
    delete_row_cnt_ += other.delete_row_cnt_;
  }
  return *this;
}


/************************************* ObTableQueuingModeCfg *************************************/
const ObTableQueuingModeCfg& ObTableQueuingModeCfg::get_basic_config(const QueuingMode mode)
{
  static const ObTableQueuingModeCfg QUEUING_MODE_CFG[static_cast<int64_t>(QueuingMode::TABLE_MODE_MAX) + 1] = {
    ObTableQueuingModeCfg(), // MODE_NORMAL            30 * 10000, 1.0      TABLE_MODE_NORMAL
    ObTableQueuingModeCfg(TABLE_MODE_QUEUING,          20 * 10000, 0.9), // TABLE_MODE_QUEUING
    ObTableQueuingModeCfg(), // MODE_NORMAL            30 * 10000, 1.0      TABLE_MODE_NORMAL
    ObTableQueuingModeCfg(TABLE_MODE_QUEUING_MODERATE, 10 * 10000, 0.8), // TABLE_MODE_QUEUING_MODERATE
    ObTableQueuingModeCfg(TABLE_MODE_QUEUING_SUPER,    5  * 10000, 0.6), // TABLE_MODE_QUEUING_SUPER
    ObTableQueuingModeCfg(TABLE_MODE_QUEUING_EXTREME,  1000,       0.5), // TABLE_MODE_QUEUING_EXTREME
    ObTableQueuingModeCfg(), // MODE_NORMAL            30 * 10000, 1.0      TABLE_MODE_NORMAL
  };
  // NOTE: If update ObTableModeFlag, please also update QUEUING_MODE_CFG, otherwise complie/static assert error
  STATIC_ASSERT((static_cast<int64_t>(QueuingMode::TABLE_MODE_MAX) + 1) == ARRAYSIZEOF(QUEUING_MODE_CFG), "table mode cnt mismatch");
  return QUEUING_MODE_CFG[mode];
}

int64_t ObTableQueuingModeCfg::get_memtable_alive_threshold(const int64_t fast_freeze_interval) const
{
  // avoid master bmsql performance degradation 5% for more frequently fast freeze, see bug/55012541
  const int64_t base_fast_freeze_interval = is_queuing_mode() ? (fast_freeze_interval * 0.4) : fast_freeze_interval;
  return base_fast_freeze_interval * queuing_factor_;
}

int64_t ObTableQueuingModeCfg::get_tombstone_row_threshold(const int64_t tombstone_default_cnt) const
{
  return tombstone_default_cnt * queuing_factor_;
}

/************************************* ObTabletStatAnalyzer *************************************/
ObTabletStatAnalyzer::ObTabletStatAnalyzer()
  : tablet_stat_(),
    boost_factor_(1),
    is_low_resource_(false)
{
}

bool ObTabletStatAnalyzer::is_hot_tablet() const
{
  return tablet_stat_.query_cnt_ + tablet_stat_.merge_cnt_ >= ACCESS_FREQUENCY * boost_factor_;
}

bool ObTabletStatAnalyzer::is_insert_mostly() const
{
  bool bret = false;
  ObTabletID tablet_id(tablet_stat_.tablet_id_);
  uint64_t total_row_cnt = tablet_stat_.get_total_merge_row_count();

  if (tablet_id.is_inner_tablet() || tablet_id.is_ls_inner_tablet()) {
    // do nothing
  } else if (0 == tablet_stat_.insert_row_cnt_) {
    // no insert occurs
  } else if (total_row_cnt < MERGE_BASIC_ROW_CNT * boost_factor_) {
    // do nothing
  } else {
    bret = total_row_cnt * LOAD_THRESHOLD <= tablet_stat_.insert_row_cnt_ * BASE_FACTOR;
  }
  return bret;
}

bool ObTabletStatAnalyzer::is_update_or_delete_mostly() const
{
  bool bret = false;
  uint64_t total_row_cnt = tablet_stat_.get_total_merge_row_count();

  if (0 == tablet_stat_.delete_row_cnt_ + tablet_stat_.update_row_cnt_) {
    // no update && delete occurs
  } else if (total_row_cnt < MERGE_BASIC_ROW_CNT * boost_factor_) {
    // do nothing
  } else {
    bret = total_row_cnt * TOMBSTONE_THRESHOLD * boost_factor_ <= (tablet_stat_.update_row_cnt_ + tablet_stat_.delete_row_cnt_) * BASE_FACTOR;
  }
  return bret;
}

bool ObTabletStatAnalyzer::has_slow_query() const
{
  bool bret = false;
  // all tablet query stats are ineffecient, only check the basic threshold
  if (tablet_stat_.scan_physical_row_cnt_ >= QUERY_BASIC_ROW_CNT * boost_factor_ ||
      tablet_stat_.scan_micro_block_cnt_ >= QUERY_BASIC_MICRO_BLOCK_CNT * boost_factor_ ||
      tablet_stat_.exist_row_total_table_cnt_ >= QUERY_BASIC_ITER_TABLE_CNT * boost_factor_) {
    bret = true;
  }
  return bret;
}

bool ObTabletStatAnalyzer::has_accumnulated_delete() const
{
  bool bret = false;
  if (is_queuing_table_mode(mode_)) {
    const ObTableQueuingModeCfg &queuing_cfg = ObTableQueuingModeCfg::get_basic_config(mode_);
    const int64_t adaptive_threshold = compaction::ObAdaptiveMergePolicy::TOMBSTONE_ROW_COUNT_THRESHOLD * queuing_cfg.queuing_factor_;
    bret = total_tablet_stat_.delete_row_cnt_ >= queuing_cfg.total_delete_row_cnt_
        || tablet_stat_.update_row_cnt_ + tablet_stat_.delete_row_cnt_ > adaptive_threshold;
  }
  return bret;
}

/************************************* ObRuntimeSysStat *************************************/
ObRuntimeSysStat::ObRuntimeSysStat()
  : min_cpu_cnt_(0),
    max_cpu_cnt_(0),
    memory_budget_(0)
{
}

void ObRuntimeSysStat::reset()
{
  min_cpu_cnt_ = 0;
  max_cpu_cnt_ = 0;
  memory_budget_ = 0;
}

bool ObRuntimeSysStat::is_low_resource() const
{
  bool bret = false;
  // 8c16g
  const int64_t cpu_threshold = 8;
  // Preserve the effective-memory scale after increasing the automatic
  // memory budget from 50% to 80%.
  const int64_t mem_threshold = (8LL << 30) * 36 / 25;
  bret = max_cpu_cnt_ < cpu_threshold || memory_budget_ < mem_threshold;
  return bret;
}

int ObRuntimeSysStat::refresh(const bool force_refresh /*=false*/)
{
  int ret = OB_SUCCESS;

  if (!REACH_THREAD_TIME_INTERVAL(300_s) && !force_refresh) {
  } else {
    min_cpu_cnt_ = share::server_runtime()->min_cpu();
    max_cpu_cnt_ = share::server_runtime()->max_cpu();
    memory_budget_ = lib::get_memory_budget();
  }
  return ret;
}


/************************************* ObTabletStream *************************************/
ObTabletStream::ObTabletStream()
  : key_(),
    total_stat_(),
    curr_buckets_(CURR_BUCKET_STEP),
    latest_buckets_(LATEST_BUCKET_STEP),
    past_buckets_(PAST_BUCKET_STEP)
{
}

ObTabletStream::~ObTabletStream()
{
  reset();
}

void ObTabletStream::reset()
{
  key_.reset();
  clear_stat();
}

void ObTabletStream::clear_stat()
{
  total_stat_.reset();
  curr_buckets_.reset();
  latest_buckets_.reset();
  past_buckets_.reset();
}

void ObTabletStream::add_stat(const ObTabletStat &stat)
{
  if (!key_.is_valid()) {
    key_.tablet_id_ = stat.tablet_id_;
  }

  if (key_.tablet_id_.id() == stat.tablet_id_) {
    curr_buckets_.add(stat);
    total_stat_ += stat;
  }
}

void ObTabletStream::refresh()
{
  ObTabletStat tablet_stat;
  bool has_retired_stat = false;

  curr_buckets_.refresh(tablet_stat, has_retired_stat);
  latest_buckets_.refresh(tablet_stat, has_retired_stat);
  past_buckets_.refresh(tablet_stat, has_retired_stat);
}

int ObTabletStream::get_all_tablet_stat(common::ObIArray<ObTabletStat> &tablet_stats) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_bucket_tablet_stat(curr_buckets_, tablet_stats))) {
  } else if (OB_FAIL(get_bucket_tablet_stat(latest_buckets_, tablet_stats))) {
  } else if (OB_FAIL(get_bucket_tablet_stat(past_buckets_, tablet_stats))) {
  }
  return ret;
}


/************************************* ObTabletStreamPool *************************************/
ObTabletStreamPool::ObTabletStreamPool()
  : dynamic_allocator_{},
    lru_list_(),
    max_dynamic_node_num_(0),
    allocated_dynamic_num_(0),
    is_inited_(false)
{
}

ObTabletStreamPool::~ObTabletStreamPool()
{
  destroy();
}

void ObTabletStreamPool::destroy()
{
  is_inited_ = false;
  ObTabletStreamNode *node = nullptr;

  DLIST_REMOVE_ALL_NORET(node, lru_list_) {
    lru_list_.remove(node);
    node->~ObTabletStreamNode();
    // ObFIFOAllocator::reset does not release memory by default.
    dynamic_allocator_.free(node);
    node = nullptr;
  }
  lru_list_.reset();

  dynamic_allocator_.reset();
  max_dynamic_node_num_ = 0;
  allocated_dynamic_num_ = 0;
}

int ObTabletStreamPool::init(const int64_t max_dynamic_node_num)
{
  int ret = OB_SUCCESS;
  const char *LABEL = "IncTbltStream";

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletStreamPool has been inited", K(ret));
  } else if (max_dynamic_node_num <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), K(max_dynamic_node_num));
  } else if (OB_FAIL(dynamic_allocator_.init(lib::ObMallocAllocator::get_instance(), OB_MALLOC_NORMAL_BLOCK_SIZE,
                                             ObMemAttr(LABEL)))) {
  } else {
    max_dynamic_node_num_ = max_dynamic_node_num;
    is_inited_ = true;
  }
  if (OB_FAIL(ret)) {
    destroy();
  }
  return ret;
}

int ObTabletStreamPool::alloc(ObTabletStreamNode *&free_node, bool &is_retired)
{
  int ret = OB_SUCCESS;
  is_retired = false;
  void *buf = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStreamPool not inited", K(ret));
  } else if (OB_NOT_NULL(free_node)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid argument", K(ret), K(free_node));
  } else if (allocated_dynamic_num_ >= max_dynamic_node_num_) {
    if (OB_UNLIKELY(lru_list_.is_empty())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lru list is unexpected null", K(ret));
    } else {
      free_node = lru_list_.get_last();
      is_retired = true;
    }
  } else if (OB_ISNULL(buf = dynamic_allocator_.alloc(sizeof(ObTabletStreamNode)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("failed to allocate memory for tablet stream node", K(ret));
  } else {
    free_node = new (buf) ObTabletStreamNode();
    ++allocated_dynamic_num_;
  }
  return ret;
}

void ObTabletStreamPool::free(ObTabletStreamNode *node)
{
  if (OB_NOT_NULL(node)) {
    int tmp_ret = OB_SUCCESS;
    if (IS_NOT_INIT) {
      tmp_ret = OB_NOT_INIT;
      LOG_ERROR_RET(tmp_ret, "[MEMORY LEAK] ObTabletStreamPool is not inited, cannot free this node!!!",
          K(tmp_ret), KPC(node));
    } else {
      node->~ObTabletStreamNode();
      dynamic_allocator_.free(node);
      --allocated_dynamic_num_;
    }
  }
}


/************************************* ObTabletStatMgr *************************************/
ObTabletStatMgr::ObTabletStatMgr()
  : report_stat_task_(*this),
    stream_pool_(),
    stream_map_(),
    bucket_lock_(),
    report_queue_(),
    sys_stat_(),
    report_cursor_(0),
    pending_cursor_(0),
    report_timer_(),
    extreme_tablet_cnt_(0),
    is_inited_(false)
{
}

ObTabletStatMgr::~ObTabletStatMgr()
{
  destroy();
}

int ObTabletStatMgr::init()
{
  int ret = OB_SUCCESS;
  const bool repeat = true;
  int64_t bucket_num = DEFAULT_BUCKET_NUM;

  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletStatMgr init twice", K(ret));
  } else if (OB_FAIL(stream_pool_.init(DEFAULT_UP_LIMIT_STREAM_CNT))) {
  } else if (OB_FAIL(stream_map_.create(DEFAULT_BUCKET_NUM, ObMemAttr("TabletStats")))) {
  } else if (FALSE_IT(bucket_num = stream_map_.bucket_count())) {
  } else if (OB_FAIL(bucket_lock_.init(bucket_num, ObLatchIds::DEFAULT_BUCKET_LOCK,
                                       ObMemAttr("TabStatMgrLock")))) {
  } else if (OB_FAIL(report_timer_.init(
      "TabletStatRpt", ObMemAttr("TabletStatRpt")))) {
  } else if (OB_FAIL(report_timer_.schedule(report_stat_task_, TABLET_STAT_PROCESS_INTERVAL, repeat))) {
  } else {
    refresh_sys_stat();
    is_inited_ = true;
  }
  if (!is_inited_) {
    reset();
  }
  return ret;
}

int ObTabletStatMgr::server_module_init(ObTabletStatMgr* &tablet_stat_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_stat_mgr->init())) {
  } else {
    LOG_INFO("success to init ObTabletStatMgr");
  }
  return ret;
}

void ObTabletStatMgr::wait()
{
  if (report_timer_.inited()) {
    report_timer_.wait();
  }
}

void ObTabletStatMgr::stop()
{
  if (report_timer_.inited()) {
    report_timer_.stop();
  }
}

void ObTabletStatMgr::destroy()
{
  if (IS_INIT) {
    reset();
  }
}

void ObTabletStatMgr::reset()
{
  stop();
  wait();
  report_timer_.destroy();
  {
    ObBucketWLockAllGuard lock_guard(bucket_lock_);
    stream_map_.destroy();
    stream_pool_.destroy();
    report_cursor_ = 0;
    pending_cursor_ = 0;
    is_inited_ = false;
  }
  bucket_lock_.destroy();
  sys_stat_.reset();
  extreme_tablet_cnt_ = 0;
  FLOG_INFO("ObTabletStatMgr destroyed!");
}

int ObTabletStatMgr::report_stat(
    const ObTabletStat &stat,
    bool &succ_report)
{
  int ret = OB_SUCCESS;
  succ_report = false;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!stat.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(stat));
  } else if (!stat.check_need_report()) {
  } else {
    uint64_t pending_cur = pending_cursor_;
    if (pending_cur - report_cursor_ >= DEFAULT_MAX_PENDING_CNT) { // first check full queue with dirty read
      if (REACH_THREAD_TIME_INTERVAL(10 * 1000L * 1000L/*10s*/)) {
        LOG_INFO("report_queue is full, wait to process", K(report_cursor_), K(pending_cur), K(stat));
      }
    } else if (FALSE_IT(pending_cur = ATOMIC_FAA(&pending_cursor_, 1))) {
    } else if (pending_cur - report_cursor_ >= DEFAULT_MAX_PENDING_CNT) { // double check
        if (REACH_THREAD_TIME_INTERVAL(10 * 1000L * 1000L/*10s*/)) {
        LOG_INFO("report_queue is full, wait to process", K(report_cursor_), K(pending_cur), K(stat));
      }
    } else {
      report_queue_[pending_cur % DEFAULT_MAX_PENDING_CNT] = stat;
      succ_report = true;
    }
  }
  return ret;
}

int ObTabletStatMgr::get_latest_tablet_stat(
    const common::ObTabletID &tablet_id,
    ObTabletStat &tablet_stat,
    ObTabletStat &total_tablet_stat,
    share::schema::ObTableModeFlag &mode)
{
  int ret = OB_SUCCESS;
  tablet_stat.reset();
  tablet_stat.tablet_id_ = tablet_id.id();
  const ObTabletStatKey key(tablet_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(tablet_id));
  } else {
    ObTabletStreamNode *stream_node = nullptr;
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(stream_map_.get_refactored(key, stream_node))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to get history stat", K(ret), K(key));
      }
    } else {
      stream_node->stream_.get_latest_stat(tablet_stat);
      total_tablet_stat = stream_node->stream_.get_total_stats();
      mode = stream_node->mode_;
    }
  }
  return ret;
}

int ObTabletStatMgr::clear_tablet_stat(
    const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;
  const ObTabletStatKey key(tablet_id);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(tablet_id));
  } else {
    ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(inner_clear_tablet_stat(key))) {
    }
  }
  if (OB_SUCC(ret)) {
    FLOG_INFO("clear tablet stat", K(ret), K(tablet_id));
  }
  return ret;
}

int ObTabletStatMgr::get_all_tablet_stats(
    common::ObIArray<ObTabletStat> &tablet_stats)
{
  int ret = OB_SUCCESS;
  tablet_stats.reset();
  ObTabletStreamNode *cur_node = nullptr;
  ObTabletStat cur_stat;
  for (auto bucket_it = stream_map_.begin(); OB_SUCC(ret) && bucket_it != stream_map_.end(); ++bucket_it) {
    if (OB_NOT_NULL(cur_node = bucket_it->second)) {
      cur_stat.reset();
      cur_node->stream_.get_latest_stat(cur_stat);
      if (is_queuing_table_mode(cur_node->mode_)) {
        if (OB_FAIL(tablet_stats.push_back(cur_stat))) {
        }
      } else if (!cur_stat.is_valid()) {
      } else if (0 == cur_stat.query_cnt_ && 0 == cur_stat.merge_cnt_) {
        // no tablet stat has been collected in the past 16 minutes.
      } else if (OB_FAIL(tablet_stats.push_back(cur_stat))) {
      }
    }
  }
  return ret;
}


int ObTabletStatMgr::get_tablet_analyzer(
    const common::ObTabletID &tablet_id,
    ObTabletStatAnalyzer &analyzer)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(get_latest_tablet_stat(tablet_id, analyzer.tablet_stat_, analyzer.total_tablet_stat_, analyzer.mode_))) {
    if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("failed to get latest tablet stat", K(ret), K(tablet_id));
    }
  } else {
    const ObTableQueuingModeCfg &queuing_cfg = ObTableQueuingModeCfg::get_basic_config(analyzer.mode_);
    analyzer.is_low_resource_ = sys_stat_.is_low_resource();
    analyzer.boost_factor_ = (analyzer.is_low_resource_ ? 2 : 1) * queuing_cfg.queuing_factor_;
  }
  return ret;
}

int ObTabletStatMgr::inner_clear_tablet_stat(const ObTabletStatKey &key)
{
  int ret = OB_SUCCESS;
  ObTabletStreamNode *stream_node = nullptr;
  if (OB_FAIL(stream_map_.get_refactored(key, stream_node))) {
    if (OB_HASH_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
    } else {
      LOG_WARN("failed to get stream node", K(ret));
    }
  } else {
    // clear statistics but remain node, otherwise table mode of queuing table will be reset
    stream_node->clear_stat();
  }
  return ret;
}

int ObTabletStatMgr::batch_clear_tablet_stat(
    const ObIArray<ObTabletID> &tablet_ids)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t tablet_cnt = tablet_ids.count();
  int64_t clear_cnt = 0;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(tablet_ids.empty())) {
  } else {
    ObTabletStatKey key;
    ObBucketWLockAllGuard lock_guard(bucket_lock_);
    for (int64_t idx = 0; idx < tablet_cnt; idx++) {
      key.tablet_id_ = tablet_ids.at(idx);
      if (OB_UNLIKELY(!key.is_valid())) {
        tmp_ret = OB_INVALID_ARGUMENT;
        LOG_WARN("get invalid tablet id", K(tmp_ret), K(key));
      } else if (OB_TMP_FAIL(inner_clear_tablet_stat(key))) {
      } else {
        clear_cnt++;
      }
    }
    if (OB_SUCC(ret)) {
      FLOG_INFO("batch clear tablet stat", K(ret), K(tablet_cnt), K(clear_cnt));
    }
  }
  return ret;
}

int ObTabletStatMgr::update_tablet_stream(const ObTabletStat &report_stat)
{
  int ret = OB_SUCCESS;
  ObTabletStreamNode *stream_node = nullptr;
  ObTabletStatKey key(report_stat.tablet_id_);
  {
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    ret = stream_map_.get_refactored(key, stream_node);
  }

  if (OB_SUCC(ret)) {
  } else if (OB_HASH_NOT_EXIST == ret) {
    ret = OB_SUCCESS;
    if (OB_FAIL(fetch_node(stream_node))) {
    } else {
      ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
      if (OB_FAIL(stream_map_.set_refactored(key, stream_node))) {
      }
    }
  } else {
    LOG_WARN("failed to get stream node from stream map", K(ret), K(key));
  }

  if (OB_SUCC(ret)) {
    if (OB_ISNULL(stream_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("stream node is unexpected null", K(ret), K(report_stat));
    } else if (OB_UNLIKELY(!stream_pool_.update_lru_list(stream_node))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to add node to lru list", K(ret), K(stream_node));
    } else {
      ObBucketHashWLockGuard lock_guard(bucket_lock_, key.hash());
      stream_node->stream_.add_stat(report_stat);
    }
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(stream_node)) {
    stream_pool_.remove_lru_list(stream_node);
    stream_pool_.free(stream_node);
    stream_node = nullptr;
  }
  return ret;
}

int ObTabletStatMgr::fetch_node(ObTabletStreamNode *&node)
{
  int ret = OB_SUCCESS;
  bool is_retired = false;
  node = nullptr;
  if (OB_FAIL(stream_pool_.alloc(node, is_retired))) {
  } else if (is_retired) { // get node from lru_list, should retire the old stat
    ObTabletStatKey old_key = node->stream_.get_tablet_stat_key();
    ObBucketHashWLockGuard lock_guard(bucket_lock_, old_key.hash());
    if (OB_FAIL(stream_map_.erase_refactored(old_key))) {
    } else {
      node->reset();
    }
  } else if (OB_UNLIKELY(!stream_pool_.add_lru_list(node))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to add node to lru list", K(ret), KPC(node));
    stream_pool_.free(node);
  }

  if (OB_FAIL(ret) && OB_NOT_NULL(node)) {
    node = nullptr;
  }
  return ret;
}

void ObTabletStatMgr::process_stats()
{
  int ret = OB_SUCCESS;
  const uint64_t start_idx = report_cursor_;
  const uint64_t pending_cur = ATOMIC_LOAD(&pending_cursor_);
  uint64_t end_idx = (pending_cur > start_idx + DEFAULT_MAX_PENDING_CNT)
                   ? start_idx + DEFAULT_MAX_PENDING_CNT
                   : pending_cur;
  if (start_idx == end_idx) { // empty queue
  } else {
    for (uint64_t i = start_idx; i < end_idx; ++i) {
      const ObTabletStat &cur_stat = report_queue_[i % DEFAULT_MAX_PENDING_CNT];
      if (OB_UNLIKELY(!cur_stat.is_valid())) {
        // allow dirty read
      } else if (OB_FAIL(update_tablet_stream(cur_stat))) {
        LOG_ERROR_RET(ret, "failed to update tablet stat", K(ret), K(cur_stat));
      }
    }
    report_cursor_ = pending_cur; // only TabletStatUpdater update this value.
  }
}

void ObTabletStatMgr::refresh_all(const int64_t step)
{
  ObBucketWLockAllGuard lock_guard(bucket_lock_);

  TabletStreamMap::iterator iter = stream_map_.begin();
  for ( ; iter != stream_map_.end(); ++iter) {
    for (int64_t i = 0; i < step; ++i) {
      iter->second->stream_.refresh();
    }
  }
}

void ObTabletStatMgr::refresh_sys_stat()
{
  (void) sys_stat_.refresh();
}

void ObTabletStatMgr::refresh_queuing_mode()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  int64_t cost_time = common::ObTimeUtility::current_time();
  int64_t stream_cnt = 0;
  int64_t update_schema_cnt = 0;
  int64_t schema_version = OB_INVALID_VERSION;

  ObMultiVersionSchemaService *schema_service = ::oceanbase::share::server_service<::oceanbase::share::schema::ObSchemaRuntimeService>()->get_schema_service();
  ObSchemaGetterGuard schema_guard;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr is not inited", K(ret));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("failed to get get schema service", K(ret));
  } else if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_schema_version(schema_version))) {
  } else {
    ObBucketWLockAllGuard lock_guard(bucket_lock_);
    stream_cnt = stream_map_.size();
    int64_t cur_extreme_table_cnt = 0;
    if (stream_cnt > 0) {
      ObSEArray<ObTabletID, 64> tablet_ids;
      ObSEArray<uint64_t, 64> table_ids;
      tablet_ids.reserve(stream_cnt);
      table_ids.reserve(stream_cnt);
      common::hash::ObHashMap<uint64_t, ObTableModeFlag> table_mode_map;
      TabletStreamMap::iterator iter = stream_map_.begin();
      for ( ; iter != stream_map_.end() && OB_SUCC(ret); ++iter) {
        if (OB_FAIL(tablet_ids.push_back(iter->first.tablet_id_))) {
        }
      }

      // TODO(chengkong): basical implement, can optimize it
      if (FAILEDx(schema_service->get_tablet_to_table_history(tablet_ids, schema_version, table_ids))) {
        LOG_WARN("failed to get table ids according to tablet ids", K(ret), K(schema_version));
      } else if (OB_UNLIKELY(tablet_ids.count() != stream_cnt || table_ids.count() != stream_cnt)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("get unexpected tablet ids or table ids", K(ret), K(tablet_ids), K(table_ids));
      } else if (OB_FAIL(table_mode_map.create(DEFAULT_BUCKET_NUM, ObMemAttr("TabStatModeMap")))) {
      } else {
        iter = stream_map_.begin();
        ObTabletStreamNode *stream_node = nullptr;
        const ObSimpleTableSchemaV2 *table_schema = nullptr;
        for (int64_t idx = 0; idx < stream_cnt && iter != stream_map_.end() && OB_SUCC(ret); ++idx, ++iter) {
          const ObTabletStatKey &key = iter->first;
          stream_node = iter->second;
          int64_t table_id = table_ids.at(idx);
          ObTableModeFlag tmp_mode_flag = TABLE_MODE_MAX;
          if (OB_UNLIKELY(OB_INVALID_ID == table_id)) {
            // TODO(chengkong): tablet id may be invalid in some cases like offline ddl or drop table.
            LOG_WARN("failed to fetch table id from inner table, may be recycled or never exists, skip it", "tablet_id", tablet_ids.at(idx));
          } else if (OB_UNLIKELY(key.tablet_id_ != tablet_ids.at(idx))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("key mismatch with tablet id", K(ret), K(key), K(tablet_ids.at(idx)));
          } else if (OB_FAIL(table_mode_map.get_refactored(table_id, tmp_mode_flag))) {
            if (OB_HASH_NOT_EXIST == ret) {
              if (OB_FAIL(schema_guard.get_simple_table_schema( table_id, table_schema))) {
              } else if (OB_ISNULL(table_schema)) {
                LOG_WARN("get nullptr table schema, skip this tablet", K(table_id));
              } else if (FALSE_IT(tmp_mode_flag = table_schema->get_table_mode_flag())) {
              } else if (FALSE_IT(stream_node->mode_ = tmp_mode_flag)) {
              } else if (FALSE_IT(update_schema_cnt++)) {
              } else if (OB_TMP_FAIL(table_mode_map.set_refactored(table_id, tmp_mode_flag))) {
              }
            } else {
              LOG_WARN("failed to get table mode from map", K(ret), K(table_id));
            }
          } else {
            stream_node->mode_ = tmp_mode_flag;
            update_schema_cnt++;
          }
          if (ObTableModeFlag::TABLE_MODE_QUEUING_EXTREME == tmp_mode_flag) {
            cur_extreme_table_cnt++;
          }
          // prevent hunging schema memory too long
          if (OB_SUCC(ret) && (idx+1) % MAX_SCHEMA_GUARD_REFRESH_CNT == 0) {
            schema_guard.reset();
            if (OB_FAIL(schema_service->get_runtime_schema_guard(schema_guard))) {
            }
          }
        }
      }
    }
    extreme_tablet_cnt_ = cur_extreme_table_cnt; // replace udpate, prevent merge schedule thread see 0
  }
  cost_time = common::ObTimeUtility::current_time() - cost_time;
  LOG_INFO("refresh queuing mode", K(ret), K(stream_cnt), K(update_schema_cnt), K_(extreme_tablet_cnt), K(cost_time));
}

int ObTabletStatMgr::get_queuing_cfg(
    const common::ObTabletID &tablet_id,
    ObTableQueuingModeCfg& queuing_cfg)
{
  int ret = OB_SUCCESS;
  const ObTabletStatKey key(tablet_id);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletStatMgr not inited", K(ret));
  } else if (OB_UNLIKELY(!key.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid arguments", K(ret), K(tablet_id));
  } else {
    ObTabletStreamNode *stream_node = nullptr;
    ObBucketHashRLockGuard lock_guard(bucket_lock_, key.hash());
    if (OB_FAIL(stream_map_.get_refactored(key, stream_node))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("failed to get history stat", K(ret), K(key));
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      queuing_cfg = ObTableQueuingModeCfg::get_basic_config(stream_node->mode_);
    }
  }
  return ret;
}

void ObTabletStatMgr::TabletStatUpdater::runTimerTask()
{
  mgr_.process_stats();
  mgr_.refresh_sys_stat();
  ObGlobalIteratorPool *global_iter_pool = ::oceanbase::share::server_service<::oceanbase::storage::ObGlobalIteratorPool>();
  if (nullptr != global_iter_pool && global_iter_pool->is_valid()) {
    global_iter_pool->wash();
  }

  int64_t interval_step = 0;
  if (CHECK_SCHEDULE_TIME_INTERVAL(CHECK_INTERVAL, interval_step)) {
    if (OB_UNLIKELY(interval_step > 1)) {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "tablet streams not refresh too long", K(interval_step));
    }
    mgr_.refresh_all(interval_step);
    mgr_.refresh_queuing_mode();
    last_update_time_ = ObTimeUtility::current_time();
    FLOG_INFO("TabletStatMgr refresh all tablet stream", K(interval_step), KPC(global_iter_pool));
  }
}
