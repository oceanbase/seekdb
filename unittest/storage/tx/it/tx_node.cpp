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

#define USING_LOG_PREFIX TRANS
#include "tx_node.h"
#define FAST_FAIL() \
  do {                                                          \
  if (OB_FAIL(ret)) {                                           \
    TRANS_LOG(ERROR, "[tx node crash] fast-fail for easy debug", K(ret)); \
    ob_abort();                                                 \
  }                                                             \
} while(0);

namespace oceanbase {
namespace share
{
void* ObMemstoreAllocator::alloc(AllocHandle& handle, int64_t size, const int64_t expire_ts)
{
  int ret = OB_SUCCESS;
  int64_t align_size = upper_align(size, sizeof(int64_t));
  bool is_out_of_mem = false;
  if (!handle.is_id_valid()) {
    COMMON_LOG(TRACE, "MTALLOC.first_alloc", KP(&handle.mt_));
    LockGuard guard(lock_);
    if (!handle.is_id_valid()) {
      handle.set_clock(arena_.retired());
      hlist_.set_active(handle);
    }
  }
  return arena_.alloc(handle.id_, handle.arena_handle_, align_size);
}
}

namespace storage {

int ObTxTable::online()
{
  ATOMIC_INC(&epoch_);
  ATOMIC_STORE(&state_, TxTableState::ONLINE);
  return OB_SUCCESS;
}

}  // namespace storage

namespace transaction {

ObTxDescGuard::~ObTxDescGuard() {
  if (tx_desc_) {
    release();
  }
}
int ObTxDescGuard::release() {
  int ret = OB_SUCCESS;
  if (tx_desc_) {
    tx_node_->release_tx(*tx_desc_);
    tx_desc_ = NULL;
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

ObString ObTxNode::get_identifer_str()
{
  const int64_t pos = addr_.to_string(buf_, sizeof(buf_));
  return ObString(pos, buf_);
}
ObTxNode::ObTxNode(const ObAddr &addr,
                   MsgBus &msg_bus) :
  name_(name_buf_),
  addr_(addr),
  runtime_state_(),
  memtable_(NULL),
  msg_consumer_(get_identifer_str(),
                &msg_queue_,
                std::bind(&ObTxNode::handle_msg_,
                          this, std::placeholders::_1)),
  t3m_(),
  lock_memtable_(),
  fake_tx_log_adapter_(nullptr),
  owns_log_adapter_(false)
{
  addr.to_string(name_buf_, sizeof(name_buf_));
  msg_consumer_.set_name(name_);
  provider_.memstore_freezer_ = &fake_memstore_freezer_;
  fake_shared_mem_alloc_mgr_.init();
  provider_.shared_mem_alloc_mgr_ = &fake_shared_mem_alloc_mgr_;
  share::g_mp = &provider_;
  runtime_state_.init();
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  ObTableHandleV2 lock_memtable_handle;
  lock_memtable_handle.set_table(&lock_memtable_, &t3m_, ObITable::LOCK_MEMTABLE);
  lock_memtable_.key_.table_type_ = ObITable::LOCK_MEMTABLE;
  fake_ls_.ls_tablet_svr_.lock_memtable_mgr_.t3m_ = &t3m_;
  fake_ls_.ls_tablet_svr_.lock_memtable_mgr_.add_memtable_(lock_memtable_handle);
  fake_lock_table_.is_inited_ = true;
  fake_lock_table_.parent_ = &fake_ls_;
  fake_lock_table_.lock_mt_mgr_ = &(fake_ls_.ls_tablet_svr_.lock_memtable_mgr_);
  fake_memstore_freezer_.is_inited_ = true;
  fake_memstore_freezer_.memstore_info_.is_loaded_ = true;
  fake_memstore_freezer_.memstore_info_.mem_memstore_limit_ = INT64_MAX;
  // memtable.freezer
  fake_freezer_.freeze_flag_ = 0;// is_freeze() = false;
  // txn service
  int ret = OB_SUCCESS;
  OZ(txs_.init(addr,
               &get_gti_source_(),
               &get_ts_mgr_(),
               &schema_service_));
  provider_.trans_service_ = &txs_;
  OZ(fake_opt_stat_mgr_.init());
  provider_.opt_stat_monitor_manager_ = &fake_opt_stat_mgr_;
  OZ(fake_lock_wait_mgr_.init());
  provider_.lock_wait_mgr_ = &fake_lock_wait_mgr_;
  ls_service_.is_inited_ = true;
  ls_service_.is_stopped_ = true;
  provider_.ls_service_ = &ls_service_;
  {
    ObColDesc col_desc;
    col_desc.col_id_ = 1;
    col_desc.col_type_.set_type(ObObjType::ObIntType);
    col_desc.col_order_ = common::ObOrderType::ASC;
    columns_.push_back(col_desc);
    col_desc.col_id_ = 2;
    columns_.push_back(col_desc);
  }
  OZ(msg_bus.regist(addr, *this));
  FAST_FAIL();
}
ObTxDescGuard ObTxNode::get_tx_guard() {
  int ret = OB_SUCCESS;
  ObTxDesc *tx = NULL;
  OZ(txs_.acquire_tx(tx));
  ObTxDescGuard x = ObTxDescGuard(this, tx);
  return x;
}
int ObTxNode::start() {
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;

  if (nullptr == fake_tx_log_adapter_) {
    fake_tx_log_adapter_ = new ObFakeTxLogAdapter();
    owns_log_adapter_ = true;
    OZ(fake_tx_log_adapter_->start());
  }
  get_ts_mgr_().reset();
  OZ(msg_consumer_.start());
  OZ(txs_.start());
  OZ(txs_.tx_ctx_mgr_.create_context_manager(&fake_tx_table_,
                                             &fake_lock_table_,
                                             *fake_ls_.get_tx_svr(),
                                             (ObITxLogParam*)0x01,
                                             fake_tx_log_adapter_));
  fake_ls_.get_tx_svr()->online();
  OZ(publish_fake_ls_());
  if (OB_SUCC(ret)) {
    ObLSTxCtxMgr &tx_ctx_mgr = txs_.tx_ctx_mgr_.get_tx_ctx_manager();
    fake_tx_table_.tx_ctx_table_.ls_tx_ctx_mgr_ = &tx_ctx_mgr;
    fake_tx_table_.is_inited_ = true;
    fake_tx_table_.ls_ = &fake_ls_;
    fake_tx_table_.online();
    int tx_data_table_offset = offsetof(storage::ObTxTable, tx_data_table_);
    void* ls_tx_data_table_ptr = (void*)((int64_t)&(fake_ls_.tx_table_) + tx_data_table_offset);
    ls_tx_data_table_ptr = &fake_tx_table_.tx_data_table_;
    fake_ls_.tx_table_.is_inited_ = true;
    fake_ls_.tx_table_.online();
    fake_ls_.ls_meta_.clog_checkpoint_scn_ = share::SCN::max_scn();
    OZ(create_memtable_(100000, memtable_));
  }
  return ret;
}

void ObTxNode::dump_msg_queue_()
{
  int ret = OB_SUCCESS;
  MsgPack *msg = NULL;
  int i = 0;
  while(OB_SUCC(msg_queue_.pop((ObLink*&)msg))) {
    ++i;
    TRANS_LOG(INFO,"[dump_msg]", K(i), K(ret), KPC(this));
    ob_free((void*)const_cast<char*>(msg->body_.ptr()));
    delete msg;
  }
}

void ObTxNode::wait_all_msg_consumed()
{
  while (msg_queue_.size() > 0 || !msg_consumer_.is_idle()) {
    if (REACH_TIME_INTERVAL(200_ms)) {
      TRANS_LOG(INFO, "wait msg_queue to be empty", K(msg_queue_.size()), KPC(this));
    }
    usleep(5_ms);
  }
}

void ObTxNode::wait_tx_log_synced()
{
  while(fake_tx_log_adapter_->get_inflight_cnt() > 0) {
    if (REACH_TIME_INTERVAL(200_ms)) {
      TRANS_LOG(INFO, "wait tx log synced...", K(fake_tx_log_adapter_->get_inflight_cnt()), KPC(this));
    }
    usleep(5_ms);
  }
}

ObTxNode::~ObTxNode() __attribute__((optnone)) {
  int ret = OB_SUCCESS;
  TRANS_LOG(INFO, "destroy TxNode", KPC(this));
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  fake_tx_table_.tx_ctx_table_.ls_tx_ctx_mgr_ = nullptr;
  bool is_tx_clean = false;
  int retry_cnt = 0;
  do {
    usleep(2000);
    txs_.block_tx(is_tx_clean);
  } while(!is_tx_clean && ++retry_cnt < 1000);
  OX(txs_.stop());
  OZ(txs_.wait_());
  OZ(txs_.tx_ctx_mgr_.remove_context_manager(true));
  provider_.trans_service_ = nullptr;
  if (owns_log_adapter_ && fake_tx_log_adapter_) {
    fake_tx_log_adapter_->stop();
    fake_tx_log_adapter_->wait();
    fake_tx_log_adapter_->destroy();
  }
  msg_consumer_.stop();
  msg_consumer_.wait();
  dump_msg_queue_();
  if (memtable_) {
    delete memtable_;
    memtable_ = nullptr;
  }
  OZ(retire_fake_ls_());
  ls_service_.destroy();
  if (owns_log_adapter_ && fake_tx_log_adapter_) {
    delete fake_tx_log_adapter_;
  }
  FAST_FAIL();
  share::g_server_runtime = &share::g_bootstrap_server_runtime;
}

int ObTxNode::create_memtable_(const int64_t tablet_id, memtable::ObMemtable *&mt) {
  int ret = OB_SUCCESS;
  memtable::ObMemtable *t = new memtable::ObMemtable();
  ObITable::TableKey table_key;
  table_key.table_type_ = ObITable::DATA_MEMTABLE;
  table_key.tablet_id_ = tablet_id;
  table_key.scn_range_.start_scn_.convert_for_gts(100);
  table_key.scn_range_.end_scn_.set_max();
  ObLS *ls = nullptr;
  OZ(ls_service_.get_ls(ls));
  OZ(t->init(table_key, ls, &fake_freezer_, &fake_memtable_mgr_, 0, 0));
  if (OB_SUCC(ret)) {
    mt = t;
  } else { delete t; }
  return ret;
}

int ObTxNode::publish_fake_ls_()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ls_service_.ls_)) {
    ret = OB_INIT_TWICE;
    TRANS_LOG(WARN, "fake ls is already published", K(ret), KP(ls_service_.ls_));
  } else {
    ls_service_.ls_ = &fake_ls_;
  }
  return ret;
}

int ObTxNode::retire_fake_ls_()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(ls_service_.ls_) && ls_service_.ls_ != &fake_ls_) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(ERROR, "unexpected ls in fake ls service", K(ret), KP(ls_service_.ls_));
  } else {
    ls_service_.ls_ = nullptr;
  }
  return ret;
}

int ObTxNode::recv_msg(const ObAddr &sender, ObString &m)
{
  TRANS_LOG(INFO, "recv_msg", K(sender), "msg_ptr", OB_P(m.ptr()), KPC(this));
  int ret = OB_SUCCESS;
  auto pkt = new MsgPack(sender, m);
  OZ(msg_queue_.push(pkt));
  msg_consumer_.wakeup();
  return ret;
}

int ObTxNode::sync_recv_msg(const ObAddr &sender, ObString &m, ObString &resp)
{
  TRANS_LOG(INFO, "sync_recv_msg", K(sender), "msg_ptr", OB_P(m.ptr()), KPC(this));
  int ret = OB_SUCCESS;
  auto pkt = new MsgPack(sender, m, true);
  OZ(msg_queue_.push(pkt));
  msg_consumer_.wakeup();
  pkt->cond_.wait(pkt->cond_.get_key(), 500000);
  if (pkt->resp_ready_) {
    OX(resp = pkt->resp_);
  } else {
    ret = OB_TIMEOUT;
    TRANS_LOG(ERROR, "wait resp timeout",K(ret));
  }
  ob_free((void*)const_cast<char*>(pkt->body_.ptr()));
  delete pkt;
  return ret;
}

int ObTxNode::handle_msg_(MsgPack *pkt)
{
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  TRANS_LOG(INFO, "begin to handle_msg", "msg_ptr", OB_P(pkt->body_.ptr()), KPC(this));
  int ret = OB_NOT_SUPPORTED;
  if (pkt->is_sync_msg_) {
    pkt->resp_ready_ = true;
    pkt->cond_.signal();
  } else {
    ob_free((void*)const_cast<char*>(pkt->body_.ptr()));
    delete pkt;
  }
  TRANS_LOG(INFO, "handle_msg done", K(ret), KPC(this));
  return ret;
}

int ObTxNode::read(ObTxDesc &tx, const int64_t key, int64_t &value, const ObTxIsolationLevel iso)
{
  int ret = OB_SUCCESS;
  ObTxReadSnapshot snapshot;
  OZ(get_read_snapshot(tx,
                       iso,
                       ts_after_ms(50),
                       snapshot));
  OZ(read(snapshot, key, value));
  return ret;
}
int ObTxNode::read(const ObTxReadSnapshot &snapshot,
                   const int64_t key,
                   int64_t &value)
{
  TRANS_LOG(INFO, "read", K(key), K(snapshot), KPC(this));
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  ObStoreCtx read_store_ctx;
  read_store_ctx.ls_ = &fake_ls_;
  OZ(txs_.get_read_store_ctx(snapshot, false, 5000ll * 1000, read_store_ctx));
  // HACK, refine: mock LS's each member in some way
  read_store_ctx.mvcc_acc_ctx_.tx_table_guards_.tx_table_guard_.init(&fake_tx_table_);
  read_store_ctx.mvcc_acc_ctx_.abs_lock_timeout_ts_ = ObTimeUtility::current_time() + 5000ll * 1000;
  blocksstable::ObDatumRow row;
  {
    ObTableIterParam iter_param;
    ObArenaAllocator allocator;
    ObTableReadInfo read_info;
    const int64_t schema_version = 100;
    read_info.init(allocator, schema_version, 1, columns_, nullptr/*storage_cols_index*/);
    iter_param.table_id_ = 1;
    iter_param.tablet_id_ = 100;
    iter_param.read_info_ = &read_info;
    iter_param.out_cols_project_ = NULL;
    iter_param.agg_cols_project_ = NULL;
    iter_param.is_multi_version_minor_merge_ = false;
    iter_param.need_scn_ = true;
    iter_param.pushdown_filter_ = NULL;
    iter_param.vectorized_enabled_ = false;

    storage::ObTableAccessContext access_context;
    {
      access_context.is_inited_ = true;
      access_context.use_fuse_row_cache_ = false;
      access_context.need_scn_ = true;
      access_context.store_ctx_ = &read_store_ctx;
      access_context.query_flag_.read_latest_ = true;
      access_context.stmt_allocator_ = &allocator;
      access_context.allocator_ = &allocator;
    }
    ObDatumRowkey row_key;
    ObStorageDatum row_key_obj;
    ObObj key_obj;
    row_key_obj.set_int(key);
    key_obj.set_int(key);
    row_key.assign(&row_key_obj, 1);
    row_key.store_rowkey_.assign(&key_obj, 1);
    OZ(memtable_->get(iter_param, access_context, row_key, row));
    STORAGE_LOG(INFO, "read_result", K(row), KPC(this));
  }
  OZ(txs_.revert_store_ctx(read_store_ctx));
  if (OB_SUCC(ret)) {
    if (row.row_flag_.is_exist()) {
      ObArenaAllocator allocator;
      blocksstable::ObNewRowBuilder new_row_builder;
      storage::ObStoreRow store_row;
      OZ(new_row_builder.init(columns_, allocator));
      OZ(new_row_builder.build_store_row(row, store_row));
      OX(value = store_row.row_val_.cells_[1].get_int());
    } else if (row.row_flag_.is_not_exist()) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ret = OB_ERR_UNEXPECTED;
      TRANS_LOG(WARN, "unexpected row result", K(ret), K(row.row_flag_), K(row), KPC(this));
    }
  }
  return ret;
}

int ObTxNode::atomic_write(ObTxDesc &tx, const int64_t key, const int64_t value,
                           const int64_t expire_ts, const ObTxParam &tx_param)
{
  int ret = OB_SUCCESS;
  ObTxSEQ sp;
  OZ(create_implicit_savepoint(tx, tx_param, sp, true));
  OZ(write(tx, key, value));
  if (sp.is_valid() && OB_FAIL(ret)) {
    OZ(rollback_to_implicit_savepoint(tx, sp, expire_ts, false));
  }
  return ret;
}
int ObTxNode::write(ObTxDesc &tx, const int64_t key, const int64_t value, const int16_t branch)
{
  int ret = OB_SUCCESS;
  ObTxReadSnapshot snapshot;
   OZ(get_read_snapshot(tx,
                       tx.isolation_,
                       ts_after_ms(50),
                       snapshot));
  OZ(write(tx, snapshot, key, value, branch));
  return ret;
}
int ObTxNode::write(ObTxDesc &tx,
                    const ObTxReadSnapshot &snapshot,
                    const int64_t key,
                    const int64_t value,
                    const int16_t branch)
{
  TRANS_LOG(INFO, "write", K(key), K(value), K(snapshot), K(tx), KPC(this));
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  ObStoreCtx write_store_ctx;
  auto iter = new ObTableStoreIterator();
  iter->reset();
  ObITable *mtb = memtable_;
  iter->add_table(mtb);
  write_store_ctx.ls_ = &fake_ls_;
  write_store_ctx.table_iter_ = iter;
  write_store_ctx.branch_ = branch;
  concurrent_control::ObWriteFlag write_flag;
  OZ(txs_.get_write_store_ctx(tx,
                              snapshot,
                              write_flag,
                              write_store_ctx));
  write_store_ctx.mvcc_acc_ctx_.tx_table_guards_.tx_table_guard_.init(&fake_tx_table_);
  ObArenaAllocator allocator;
  ObDatumRow row;
  ObStorageDatum cols[2] = {ObStorageDatum(), ObStorageDatum()};
  cols[0].set_int(key);
  cols[1].set_int(value);
  row.count_ = 2;
  row.storage_datums_ = cols;
  row.row_flag_ = blocksstable::ObDmlFlag::DF_UPDATE;
  row.trans_id_.reset();

  ObTableIterParam param;
  ObTableAccessContext context;
  ObVersionRange trans_version_range;
  const bool read_latest = true;
  ObQueryFlag query_flag;
  ObTableReadInfo read_info;

  const int64_t schema_version = 100;
  read_info.init(allocator, 2, 1, columns_, nullptr/*storage_cols_index*/);

  trans_version_range.base_version_ = 0;
  trans_version_range.multi_version_start_ = 0;
  trans_version_range.snapshot_version_ = EXIST_READ_SNAPSHOT_VERSION;
  query_flag.use_row_cache_ = ObQueryFlag::DoNotUseCache;
  query_flag.read_latest_ = read_latest & ObQueryFlag::OBSF_MASK_READ_LATEST;

  param.table_id_ = 1;
  param.tablet_id_ = 1;
  param.read_info_ = &read_info;

  context.init(query_flag, write_store_ctx, allocator, trans_version_range);
  const ObMemtableSetArg arg(&row,
                             &columns_,
                             NULL, /*update_idx*/
                             NULL, /*old_row*/
                             1,    /*row_count*/
                             false /*check_exist*/);
  OZ(memtable_->set(param, context, arg));
  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(txs_.revert_store_ctx(write_store_ctx))) {
    TRANS_LOG(WARN, "revert store ctx failed", KR(tmp_ret), K(write_store_ctx));
  }
  delete iter;
  return ret;
}

int ObTxNode::write_begin(ObTxDesc &tx,
                          const ObTxReadSnapshot &snapshot,
                          ObStoreCtx& write_store_ctx)
{
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  auto iter = new ObTableStoreIterator();
  iter->reset();
  ObITable *mtb = memtable_;
  iter->add_table(mtb);
  write_store_ctx.ls_ = &fake_ls_;
  write_store_ctx.table_iter_ = iter;
  concurrent_control::ObWriteFlag write_flag;
  OZ(txs_.get_write_store_ctx(tx,
                              snapshot,
                              write_flag,
                              write_store_ctx));
  return ret;
}

int ObTxNode::write_one_row(ObStoreCtx& write_store_ctx, const int64_t key, const int64_t value)
{
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;

  ObArenaAllocator allocator;
  ObTableReadInfo read_info;
  const int64_t schema_version = 100;
  read_info.init(allocator, 2, 1, columns_, nullptr/*storage_cols_index*/);
  ObDatumRow row;
  ObStorageDatum cols[2] = {ObStorageDatum(), ObStorageDatum()};
  cols[0].set_int(key);
  cols[1].set_int(value);
  row.row_flag_ = blocksstable::ObDmlFlag::DF_UPDATE;
  row.storage_datums_ = cols;
  row.count_ = 2;

  ObTableIterParam param;
  ObTableAccessContext context;
  ObVersionRange trans_version_range;
  const bool read_latest = true;
  ObQueryFlag query_flag;


  trans_version_range.base_version_ = 0;
  trans_version_range.multi_version_start_ = 0;
  trans_version_range.snapshot_version_ = EXIST_READ_SNAPSHOT_VERSION;
  query_flag.use_row_cache_ = ObQueryFlag::DoNotUseCache;
  query_flag.read_latest_ = read_latest & ObQueryFlag::OBSF_MASK_READ_LATEST;

  param.table_id_ = 1;
  param.tablet_id_ = 1;
  param.read_info_ = &read_info;

  OZ(context.init(query_flag, write_store_ctx, allocator, trans_version_range));

  const ObMemtableSetArg arg(&row,
                             &columns_,
                             NULL, /*update_idx*/
                             NULL, /*old_row*/
                             1,    /*row_count*/
                             false /*check_exist*/);

  OZ(memtable_->set(param, context, arg));

  return ret;
}

int ObTxNode::write_end(ObStoreCtx& write_store_ctx)
{
  int ret = OB_SUCCESS;
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;

  delete write_store_ctx.table_iter_;
  write_store_ctx.table_iter_ = nullptr;
  OZ(txs_.revert_store_ctx(write_store_ctx));

  return ret;
}

int ObTxNode::replay(const void *buffer,
                     const int64_t nbytes,
                     const palf::LSN &lsn,
                     const int64_t ts_ns)
{
  share::g_server_runtime = &runtime_state_;
  share::g_mp = &provider_;
  int ret = OB_SUCCESS;
  logservice::ObLogBaseHeader base_header;
  int64_t tmp_pos = 0;
  const char *log_buf = static_cast<const char *>(buffer);
  if (OB_FAIL(base_header.deserialize(log_buf, nbytes, tmp_pos))) {
    LOG_WARN("log base header deserialize error", K(ret));
  } else {
    share::SCN log_scn;
    log_scn.convert_for_tx(ts_ns);
    ObFakeTxReplayExecutor executor(&fake_ls_,
                                    fake_ls_.get_tx_svr(),
                                    lsn,
                                    log_scn,
                                    base_header);
    executor.set_memtable(memtable_);
    if (OB_FAIL(executor.execute(log_buf, nbytes, tmp_pos))) {
      LOG_WARN("replay tx log error", K(ret), K(lsn), K(ts_ns));
    } else {
      LOG_INFO("replay tx log succ", K(ret), K(lsn), K(ts_ns));
    }
  }
  return ret;
}
} // transaction
} // oceanbase
