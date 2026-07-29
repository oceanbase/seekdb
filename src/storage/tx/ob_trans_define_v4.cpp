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

#include "ob_trans_define_v4.h"
#include "ob_trans_functor.h"

#define USING_LOG_PREFIX TRANS
namespace oceanbase
{
using namespace oceanbase::share;
namespace transaction
{
ObTxIsolationLevel tx_isolation_from_str(const ObString &s)
{
  static const ObString LEVEL_NAME[4] =
    {
     "READ-UNCOMMITTED",
     "READ-COMMITTED",
     "REPEATABLE-READ",
     "SERIALIZABLE"
    };
  ObTxIsolationLevel r = ObTxIsolationLevel::INVALID;
  for (int32_t i = 0; i < 4; i++) {
    if (0 == LEVEL_NAME[i].case_compare(s)) {
      r = static_cast<ObTxIsolationLevel>(i);
      break;
    }
  }
  return r;
}


ObTxSavePoint::ObTxSavePoint()
  : type_(T::INVL), scn_(), name_() {}

ObTxSavePoint::ObTxSavePoint(const ObTxSavePoint &a)
{
  *this = a;
}

ObTxSavePoint &ObTxSavePoint::operator=(const ObTxSavePoint &a)
{
  type_ = a.type_;
  scn_ = a.scn_;
  switch(type_) {
  case T::SAVEPOINT:
  case T::STASH: {
    name_ = a.name_;
    break;
  }
  case T::SNAPSHOT: snapshot_ = a.snapshot_; break;
  default: break;
  }
  return *this;
}

ObTxSavePoint::~ObTxSavePoint()
{
  release();
}

void ObTxSavePoint::release()
{
  type_ = T::INVL;
  snapshot_ = NULL;
  scn_.reset();
}

void ObTxSavePoint::rollback()
{
  if (is_snapshot() && snapshot_) {
    snapshot_->invalid();
  }
  release();
}

void ObTxSavePoint::init(ObTxReadSnapshot *snapshot)
{
  type_ = T::SNAPSHOT;
  snapshot_ = snapshot;
  scn_ = snapshot->tx_seq();
}

int ObTxSavePoint::init(const ObTxSEQ &scn, const ObString &name, const bool stash)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(name_.assign(name))) {
    if (OB_BUF_NOT_ENOUGH == ret) {
      //rewrite ret
      ret = OB_ERR_TOO_LONG_IDENT;
    }
    TRANS_LOG(WARN, "invalid savepoint name", K(ret), K(name));
  } else {
    type_ = stash ? T::STASH : T::SAVEPOINT;
    scn_ = scn;
  }
  return ret;
}

DEF_TO_STRING(ObTxSavePoint)
{
  int64_t pos = 0;
  J_OBJ_START();
  switch(type_) {
  case T::SAVEPOINT: J_KV("savepoint", name_); break;
  case T::SNAPSHOT:  J_KV(KPC_(snapshot)); break;
  case T::STASH: J_KV("stash_savepoint", name_); break;
  default: J_KV("invalid", true);
  }
  J_COMMA();
  J_KV(K_(scn));
  J_OBJ_END();
  return pos;
}
OB_SERIALIZE_MEMBER(ObTxExecResult, incomplete_, has_write_state_, write_state_,
                    conflict_info_array_,
                    touched_storage_);
OB_SERIALIZE_MEMBER(ObTxSnapshot, tx_id_, version_, scn_, elr_);
OB_SERIALIZE_MEMBER(ObTxReadSnapshot,
                    valid_,
                    core_,
                    source_,
                    uncertain_bound_,
                    has_write_state_,
                    committed_);

int ObTxReadSnapshot::serialize_for_lob(const share::SCN &fb_snapshot, SERIAL_PARAMS) const
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, core_, source_, has_write_state_);
  OB_UNIS_ENCODE(fb_snapshot);
  return ret;
}

int ObTxReadSnapshot::deserialize_for_lob(share::SCN &fb_snapshot, DESERIAL_PARAMS)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, core_, source_, has_write_state_);
  OB_UNIS_DECODE(fb_snapshot);
  if (OB_SUCC(ret)) {
    valid_ = true;
  }
  return ret;
}

int64_t ObTxReadSnapshot::get_serialize_size_for_lob(const share::SCN &fb_snapshot) const
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, core_, source_, has_write_state_);
  OB_UNIS_ADD_LEN(fb_snapshot);
  return len;
}

int ObTxReadSnapshot::build_snapshot_for_lob(const ObTxSnapshot &core)
{
  int ret = OB_SUCCESS;
  core_ = core;
  valid_= true;
  source_ = ObTxReadSnapshot::SRC::LS;
  return ret;
}

int ObTxReadSnapshot::build_snapshot_for_lob(
    const int64_t snapshot_version,
    const int64_t snapshot_tx_id,
    const int64_t snapshot_seq)
{
  int ret = OB_SUCCESS;
  core_.version_.convert_for_tx(snapshot_version);
  core_.tx_id_ = snapshot_tx_id;
  core_.scn_ = ObTxSEQ::cast_from_int(snapshot_seq);
  valid_ = true;
  source_ = ObTxReadSnapshot::SRC::LS;
  return ret;
}

int ObTxReadSnapshot::refresh_seq_no(const int64_t tx_seq_base)
{
  int ret = OB_SUCCESS;
  if (tx_seq_base < 0) {
    ret = OB_INVALID_ARGUMENT;
    TRANS_LOG(WARN, "tx_seq_base invalid", K(ret), K(tx_seq_base));
  } else {
    core_.scn_ = core_.scn_.clone_with_seq(ObSequence::get_max_seq_no(), tx_seq_base);
  }
  return ret;
}

OB_SERIALIZE_MEMBER(ObTxWriteState, first_scn_, last_scn_, flag_.flag_val_);

DEFINE_SERIALIZE(ObTxDesc::FLAG)
{
  int ret = OB_SUCCESS;
  return serialization::encode_i64(buf, buf_len, pos, v_);
}
DEFINE_DESERIALIZE(ObTxDesc::FLAG)
{
  int ret = OB_SUCCESS;
  return serialization::decode_i64(buf, data_len, pos, (int64_t*)&v_);
}
DEFINE_GET_SERIALIZE_SIZE(ObTxDesc::FLAG)
{
  return serialization::encoded_length_i64(v_);
}

OB_SERIALIZE_MEMBER(ObTxDesc,
                    data_version_,
                    sess_id_,
                    addr_,
                    tx_id_,
                    isolation_,
                    access_mode_,
                    op_sn_,
                    state_,
                    flags_,
                    expire_ts_,
                    active_ts_,
                    timeout_us_,
                    lock_timeout_us_,
                    active_scn_,
                    has_write_state_,
                    write_state_,
                    seq_base_);
OB_SERIALIZE_MEMBER(ObTxParam,
                    timeout_us_,
                    lock_timeout_us_,
                    access_mode_,
                    isolation_);
ObTxDesc::ObTxDesc()
  : trace_info_(),
    data_version_(0),
    seq_base_(0),
    tx_consistency_type_(ObTxConsistencyType::INVALID),
    addr_(),
    tx_id_(),
    isolation_(ObTxIsolationLevel::RC), // default is RC
    access_mode_(ObTxAccessMode::INVL),   // default is INVL
    snapshot_version_(),
    last_rc_snapshot_version_(share::SCN::min_scn()),
    snapshot_uncertain_bound_(0),
    snapshot_scn_(),
    sess_id_(0),
    op_sn_(0),                          // default is from 0
    state_(State::INVL),
    flags_({ 0 }),
    state_change_flags_({ 0 }),
    alloc_ts_(-1),
    active_ts_(-1),
    timeout_us_(-1),
    lock_timeout_us_(-1),
    expire_ts_(INT64_MAX),              // never expire by default
    commit_ts_(-1),
    finish_ts_(-1),
    active_scn_(),
    min_implicit_savepoint_(),
    last_branch_id_(0),
    has_write_state_(false),
	    write_state_(),
	    savepoints_(),
	    commit_expire_ts_(0),
    commit_version_(),
    commit_out_(-1),
    commit_times_(0),
    commit_start_scn_(),
    abort_cause_(0),
    lock_(common::ObLatchIds::TX_DESC_LOCK),
    commit_cb_lock_(common::ObLatchIds::TX_DESC_COMMIT_LOCK),
    commit_cb_(NULL),
    cb_tid_(-1),
    exec_info_reap_ts_(0),
    commit_task_()
#ifdef ENABLE_DEBUG_LOG
  , alloc_link_()
#endif
{}

/**
 * Wrap txDesc to IDLE state cleanup txn dirty state
 * keep txn parameters and resource allocated before
 * txn active:
 * - savepoint
 * - txn-level snapshot
 *
 * Be Careful when you make any change here
 *
 * caller must hold txDesc.lock_
 */
int ObTxDesc::switch_to_idle()
{
  tx_id_.reset();
  trace_info_.reset();
  flags_.switch_to_idle_();
  state_change_flags_.reset();
  active_ts_ = 0;
  timeout_us_ = 0;
  lock_timeout_us_ = -1;
  expire_ts_ = INT64_MAX;
  commit_ts_ = 0;
  finish_ts_ = 0;
  active_scn_.reset();
  has_write_state_ = false;
	  write_state_ = ObTxWriteState();
  commit_version_.reset();
  commit_out_ = 0;
  commit_times_ = 0;
  commit_start_scn_.set_min();
  abort_cause_ = 0;
  commit_cb_ = NULL;
  cb_tid_ = -1;
  exec_info_reap_ts_ = 0;
  commit_task_.reset();
  state_ = State::IDLE;
  op_sn_ = 0;
  return OB_SUCCESS;
}

inline void ObTxDesc::FLAG::switch_to_idle_()
{
  v_ = 0;
}

// this function helper will update current flag with the given
// and ensure private flags will not be overriden

ObTxDesc::~ObTxDesc()
{
  reset();
}

void ObTxDesc::reset()
{
#ifndef NDEBUG
  FORCE_PRINT_TRACE(&tlog_, "[tx desc trace]");
#else
  if (state_ == State::IDLE || state_ == State::COMMITTED) {
    if (finish_ts_ - commit_ts_ > 5 * 1000 * 1000) {
      FORCE_PRINT_TRACE(&tlog_, "[tx slow commit][tx desc trace]");
    }
  } else if (flags_.SHADOW_) { /* skip clone's destory */}
  else {
    FORCE_PRINT_TRACE(&tlog_, "[tx desc trace]");
  }
#endif

  trace_info_.reset();
  data_version_ = 0;
  seq_base_ = 0;
  tx_consistency_type_ = ObTxConsistencyType::INVALID;

  addr_.reset();
  tx_id_.reset();
  isolation_ = ObTxIsolationLevel::INVALID;
  access_mode_ = ObTxAccessMode::INVL;
  snapshot_version_.reset();
  last_rc_snapshot_version_.set_min();
  snapshot_uncertain_bound_ = 0;
  snapshot_scn_.reset();

  op_sn_ = -1;

  state_ = State::INVL;

  flags_.v_ = 0;
  flags_.SHADOW_ = true;
  state_change_flags_.reset();

  alloc_ts_ = -1;
  active_ts_ = -1;
  timeout_us_ = -1;
  lock_timeout_us_ = -1;
  expire_ts_ = -1;
  commit_ts_ = -1;
  finish_ts_ = -1;

  active_scn_.reset();
  min_implicit_savepoint_.reset();
  last_branch_id_ = 0;
  has_write_state_ = false;
	  write_state_ = ObTxWriteState();
	  savepoints_.reset();
	  conflict_txs_.reset();

	  commit_expire_ts_ = -1;
  commit_version_.reset();
  commit_out_ = -1;
  commit_times_ = 0;
  commit_start_scn_.set_min();
  abort_cause_ = 0;

  commit_cb_ = NULL;
  cb_tid_ = -1;
  exec_info_reap_ts_ = 0;
  commit_task_.reset();
  tlog_.reset();
}

void ObTxDesc::set_tx_id(const ObTransID &tx_id)
{
  tx_id_ = tx_id;
}

void ObTxDesc::reset_tx_id()
{
  tx_id_.reset();
}

const ObString &ObTxDesc::get_tx_state_str() const {
  static const ObString TxStateName[] =
    {
     ObString("INVALID"),
     ObString("IDLE"),
     ObString("ACTIVE"),
     ObString("IMPLICIT_ACTIVE"),
     ObString("ROLLBACK_SAVEPOINT"),
     ObString("IN_TERMINATE"),
     ObString("ABORTED"),
     ObString("ROLLED_BACK"),
     ObString("COMMIT_TIMEOUT"),
     ObString("COMMIT_UNKNOWN"),
     ObString("COMMITTED"),
     ObString("UNNAMED STATE")
    };
  const int state = MIN((int)state_, sizeof(TxStateName) / sizeof(ObString) - 1);
  return TxStateName[state];
}

void ObTxDesc::print_trace_() const
{
  FORCE_PRINT_TRACE(&tlog_, "[tx desc trace]");
}

void ObTxDesc::print_trace()
{
  int ret = OB_SUCCESS;
  bool self_locked = lock_.self_locked();
  if (!self_locked && OB_FAIL(lock_.lock())) {
    TRANS_LOG(WARN, "lock failed", K(ret));
  } else {
    FORCE_PRINT_TRACE(&tlog_, "[tx desc trace]");
  }
  if (!self_locked && OB_SUCC(ret)) {
    lock_.unlock();
  }
}

void ObTxDesc::dump_and_print_trace()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(lock_.trylock())) {
    TRANS_LOG(WARN, "acquire lock fail", K(ret), KP(this), K(tx_id_));
  } else {
    share::ObTaskController::get().allow_next_syslog();
    TRANS_LOG(INFO, "[tx desc dump]", KPC(this));
    print_trace_();
    lock_.unlock();
  }
}

bool ObTxDesc::in_tx_or_has_extra_state() const
{
  ObSpinLockGuard guard(lock_);
  return in_tx_or_has_extra_state_();
}

bool ObTxDesc::in_tx_or_has_extra_state_() const
{
  return is_in_tx() || has_extra_state_();
}

bool ObTxDesc::has_extra_state_() const
{
  if (with_tx_snapshot()) {
    return true;
  }
  // TODO(yunxing.cyx): refine this iter for performance
  ARRAY_FOREACH_NORET(savepoints_, i) {
    if (savepoints_[i].is_savepoint()) {
      return true;
    }
  }
  return false;
}

bool ObTxDesc::in_tx_for_free_route()
{
  ObSpinLockGuard guard(lock_);
  return in_tx_for_free_route_();
}

bool ObTxDesc::in_tx_for_free_route_()
{
  return (addr_.is_valid() && (addr_ != GCONF.self_addr_)) // txn free route temporary node
    || in_tx_or_has_extra_state_();
}

bool ObTxDesc::contain_savepoint(const ObString &sp)
{
  bool hit = false;
  ARRAY_FOREACH_X(savepoints_, i, cnt, !hit) {
    ObTxSavePoint &it = savepoints_[cnt - 1 - i];
    if (it.is_savepoint() && it.name_ == sp) {
      hit = true;
    }
    if (it.is_stash()) { break; }
  }
  return hit;
}

int ObTxDesc::merge_write_state_(ObTxWriteState &a, const bool append, const bool check_only_if_exist)
{
  int ret = OB_SUCCESS;
  const bool hit = has_write_state_;
  if (exec_info_reap_ts_ == 0) {
    exec_info_reap_ts_ = ObSequence::get_max_seq_no();
  }

  if (hit) {
    ObTxWriteState &p = write_state_;
    if (OB_SUCC(ret) && !check_only_if_exist) {
      p.first_scn_ = MIN(a.first_scn_, p.first_scn_);
      p.last_scn_ = p.last_scn_.is_max() ? a.last_scn_ : MAX(a.last_scn_, p.last_scn_);
      p.last_touch_ts_ = exec_info_reap_ts_ + 1;
      if (p.is_clean() && !a.is_clean()) {
        p.flag_.set_dirty();
      }
    }
  }

  if (OB_SUCC(ret) && ObTxDesc::State::IMPLICIT_ACTIVE == state_ && !active_scn_.is_valid()) {
    /*
     * it is a first stmt's retry, we should set active scn
     * to enable recognizing it is first stmt
     */
    active_scn_ = get_tx_seq();
  }

  if (OB_FAIL(ret)) {
  } else if (!hit) {
    if (append) {
      a.last_touch_ts_ = exec_info_reap_ts_ + 1;
      write_state_ = a;
      has_write_state_ = true;
      implicit_start_tx_();
    } else {
      ret = OB_ENTRY_NOT_EXIST;
    }
  }
  state_change_flags_.WRITE_STATE_CHANGED_ = true;
  return ret;
}

void ObTxDesc::finish_write_state_rollback_(ObTxWriteState *part, const ObTxSEQ &savepoint)
{
  if (OB_NOT_NULL(part)) {
    part->last_scn_ = savepoint;
  }
  state_change_flags_.WRITE_STATE_CHANGED_ = true;
}

int ObTxDesc::update_clean_write_state()
{
  ObTxWriteState p;
  p.first_scn_ = ObTxSEQ::MAX_VAL();
  p.last_scn_ = get_tx_seq();
  return merge_write_state_(p, false);
}

int ObTxDesc::init_clean_write_state_if_absent()
{
  int ret = OB_SUCCESS;
  ObTxSEQ cur_seq = get_tx_seq();
  ObTxWriteState p;
  p.first_scn_ = cur_seq;
  p.last_scn_ = cur_seq;
  p.flag_.set_clean();
  return merge_write_state_(p, true, true);
}

/*
 * merge_write_state - update txn write state info
 *
 * if failed, txn was marked with PARTS_INCOMPLETE
 */
int ObTxDesc::merge_write_state(ObTxWriteState &p)
{
  ObSpinLockGuard guard(lock_);
  return merge_write_state_(p, true);
}

int ObTxDesc::mark_write()
{
  int ret = OB_SUCCESS;
  ObTxWriteState part;
  part.first_scn_ = ObTxSEQ::MAX_VAL();
  part.last_scn_ = ObTxSEQ::MAX_VAL();
  if (OB_FAIL(merge_write_state_(part))) {
    TRANS_LOG(WARN, "update single transaction write state failed", K(ret));
  }
  return ret;
}

void ObTxDesc::implicit_start_tx_()
{
  if (has_write_state_ && state_ == ObTxDesc::State::IDLE) {
    state_ = ObTxDesc::State::IMPLICIT_ACTIVE;
    active_ts_ = ObClockGenerator::getClock();
    expire_ts_ = get_expire_ts();
    active_scn_ = get_tx_seq();
    state_change_flags_.mark_all();
  }
}

int64_t ObTxDesc::get_expire_ts() const
{
  /*
   * expire_ts was setup when tx state switch to ACTIVE | IMPLICIT_ACTIVE
   * because create TxCtx (which need acquire tx expire_ts) happens before
   * tx state switch to IMPLICIT_ACTIVE
   */
  int64_t ret = expire_ts_;
  if (expire_ts_ == INT64_MAX || expire_ts_ <=0) { // unset
    const int64_t start_ts = active_ts_ <= 0 ? ObClockGenerator::getClock() : active_ts_;
    ret = (MAX_TRANS_TIMEOUT_US - start_ts) <= timeout_us_ ? MAX_TRANS_TIMEOUT_US : (start_ts + timeout_us_);
  }
  return ret;
}

int ObTxDesc::merge_write_state_if_present_(const ObTxWriteState &part, const bool has_write_state)
{
  int ret = OB_SUCCESS;
  if (has_write_state) {
    ObTxWriteState copied_part = part;
    ret = merge_write_state_(copied_part);
  }
  return ret;
}

int ObTxDesc::merge_exec_info_with(const ObTxDesc &src)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (OB_FAIL(merge_write_state_if_present_(src.write_state_, src.has_write_state_))) {
    TRANS_LOG(WARN, "update write state failed", K(ret), KPC(this), K(src));
  }
  if (src.flags_.WRITE_STATE_INCOMPLETE_) {
    flags_.WRITE_STATE_INCOMPLETE_ = true;
    TRANS_LOG(WARN, "src is incomplete, set dest incomplete also", K(ret), K(src));
  }
  return ret;
}

int ObTxDesc::get_inc_exec_info(ObTxExecResult &exec_info)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (exec_info_reap_ts_ >= 0) {
    if (has_write_state_ &&
        write_state_.last_touch_ts_ > exec_info_reap_ts_ &&
        OB_FAIL(exec_info.set_write_state(write_state_))) {
      TRANS_LOG(WARN, "set exec write state failed", K(ret), K_(write_state), KPC(this), K(exec_info));
    }
    if (OB_FAIL(ret) || flags_.WRITE_STATE_INCOMPLETE_) {
      exec_info.incomplete_ = true;
      TRANS_LOG(WARN, "set incomplete", K(ret), K(flags_.WRITE_STATE_INCOMPLETE_));
    }
    exec_info_reap_ts_ += 1;
  }
  if (OB_SUCC(ret) && OB_SUCC(exec_info.merge_cflict_txs(conflict_txs_))) {
    conflict_txs_.reset();
  }
  DETECT_LOG(TRACE, "merge conflict txs to exec result", K(conflict_txs_), K(exec_info));
  return ret;
}

int ObTxDesc::add_exec_info(const ObTxExecResult &exec_info)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (OB_FAIL(merge_write_state_if_present_(exec_info.write_state_, exec_info.has_write_state_))) {
    TRANS_LOG(WARN, "update write state failed", K(ret), KPC(this), K(exec_info));
  }
  if (exec_info.incomplete_) {
    flags_.WRITE_STATE_INCOMPLETE_ = true;
    TRANS_LOG(WARN, "exec_info is incomplete set incomplete also", K(ret), K(exec_info));
  }
  (void) merge_conflict_txs_(exec_info.conflict_txs_);
  DETECT_LOG(TRACE, "add exec result conflict txs to desc", K(conflict_txs_), K(exec_info));
  return ret;
}

int ObTxDesc::set_commit_cb(ObITxCallback *cb)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(commit_cb_lock_);
  if (OB_NOT_NULL(commit_cb_)) {
    ret = OB_ERR_UNEXPECTED;
    TRANS_LOG(WARN, "commit_cb not null", K(ret), KP_(commit_cb), K(tx_id_), KP(cb));
  } else {
    commit_cb_ = cb;
  }
  return ret;
}


//try to acquire commit_cb_lock only if commit_cb_ not null
inline bool ObTxDesc::acq_commit_cb_lock_if_need_()
{
  int ret = OB_SUCCESS;
  bool succ = false;
  int cnt = 0;
  do {
    ret = commit_cb_lock_.trylock();
    if (ret == OB_EAGAIN) {
      if (OB_NOT_NULL(commit_cb_)) {
        if (REACH_TIME_INTERVAL(2 * 1000 * 1000)) {
          TRANS_LOG_RET(WARN, OB_ERR_TOO_MUCH_TIME, "use too much time wait lock", K_(tx_id));
        }
        if (++cnt < 200) { PAUSE(); }
        else { ob_usleep(5000); }
      }
    } else if (OB_FAIL(ret)) {
      TRANS_LOG(ERROR, "try lock failed", K(ret), K_(tx_id));
    } else {
      succ = true;
    }
  } while (ret == OB_EAGAIN && OB_NOT_NULL(commit_cb_));
  return succ;
}

/*
 * execute_commit_cb - callback caller after commit finished
 *
 * because user supllied callback may do anything, it should take care
 * when process the callback, especially callback call into transaction
 * side again via these interfaces:
 * 1) release_tx
 * 2) reuse_tx
 *
 * in reuse_tx situ, it's required to wait all referents of current txn
 * to quit except current thread. so if two of thread is going here and
 * executing 'execute_commit_cb', the former will wait the later quit
 * while the later is blocking on 'commit_cb_lock_' to wait the former
 * quit, which introduce a deadlock.
 *
 * in order to prevent such situ, calling thread must try lock and check
 * there requirement of continue the calling procedure, as in the above
 * situ, the later thread is not required to go ahead, instead they can
 * shortcut and return.
 * for more detail, refer to 'acq_commit_cb_lock_if_need_' function.
 */
bool ObTxDesc::execute_commit_cb()
{
  bool executed = false;
  /*
   * load_acquire state_ and commit_out_
   * pair with ObTransService::handle_tx_commit_result_
   */
  ATOMIC_LOAD_ACQ((int*)&state_);
  if (is_tx_end()) {
    ObTransID tx_id = tx_id_;
    ObITxCallback *cb = commit_cb_;
    int ret = OB_SUCCESS;
     if (OB_NOT_NULL(commit_cb_) && acq_commit_cb_lock_if_need_()) {
      if (OB_NOT_NULL(commit_cb_)) {
        executed = true;
        cb = commit_cb_;
        commit_cb_ = NULL;
        if (0 <= cb_tid_) {
#ifdef ENABLE_DEBUG_LOG
          ob_abort();
#endif
          TRANS_LOG(ERROR, "unexpected error happen, cb_tid_ should smaller than 0",
                    KP(this), K(tx_id), KP(cb_tid_));
        }
        ATOMIC_STORE_REL(&cb_tid_, GETTID());
        // NOTE: it is required add trace event before callback,
        // because txDesc may be released after callback called
        REC_TRANS_TRACE_EXT(&tlog_, exec_commit_cb,
                            OB_ID(arg), (void*)cb,
                            OB_ID(ref), get_ref(),
                            OB_ID(thread_id), GETTID());
        commit_cb_lock_.unlock();
        cb->callback(commit_out_);
      } else {
        commit_cb_lock_.unlock();
      }
    }
    TRANS_LOG(TRACE, "execute_commit_cb", KP(this), K(tx_id), KP(cb), K(executed));
  }
  return executed;
}

ObITxCallback *ObTxDesc::cancel_commit_cb()
{
  int ret = OB_SUCCESS;
  ObITxCallback* commit_cb = nullptr;

  /* cancel may called from `commit_cb_` it self */
  bool self_locked = commit_cb_lock_.self_locked();
  if (!self_locked && OB_FAIL(commit_cb_lock_.lock())) {
    TRANS_LOG(ERROR, "lock failed", K(ret), K_(tx_id));
  } else {
    if (OB_NOT_NULL(commit_cb_)) {
      commit_cb = commit_cb_;
      commit_cb_ = NULL;
    }
    if (!self_locked) {
      commit_cb_lock_.unlock();
    }
  }

  return commit_cb;
}

bool ObTxDesc::has_implicit_savepoint() const
{
  return min_implicit_savepoint_.is_valid();
}
void ObTxDesc::add_implicit_savepoint(const ObTxSEQ savepoint)
{
  if (!min_implicit_savepoint_.is_valid() || min_implicit_savepoint_ > savepoint ) {
    min_implicit_savepoint_ = savepoint;
  }
}
void ObTxDesc::release_all_implicit_savepoint()
{
  min_implicit_savepoint_.reset();
}
void ObTxDesc::release_implicit_savepoint(const ObTxSEQ savepoint)
{
  if (min_implicit_savepoint_ == savepoint) {
    min_implicit_savepoint_.reset();
  }
  // invalid txn snapshot if it was created after the savepoint
  if (with_tx_snapshot() && savepoint < snapshot_scn_) {
    TRANS_LOG(INFO, "release txn snapshot_version", K_(snapshot_version),
              K(savepoint), K_(snapshot_scn), K_(tx_id));
    snapshot_version_.reset();
  }
}

int ObTxDesc::fetch_conflict_txs(ObIArray<ObTransID> &array)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (OB_FAIL(array.assign(conflict_txs_))) {
    DETECT_LOG(WARN, "fail to fetch conflict txs", K(ret), K(conflict_txs_));
  }
  conflict_txs_.reset();
  return ret;
}

int ObTxDesc::add_conflict_tx(const ObTransID &conflict_tx)
{
  ObSpinLockGuard guard(lock_);
  return add_conflict_tx_(conflict_tx);
}

int ObTxDesc::add_conflict_tx_(const ObTransID &conflict_tx)
{
  int ret = OB_SUCCESS;
  if (conflict_txs_.count() >= MAX_RESERVED_CONFLICT_TX_NUM) {
    ret = OB_SIZE_OVERFLOW;
    int64_t max_reserved_conflict_tx_num = MAX_RESERVED_CONFLICT_TX_NUM;
    DETECT_LOG(WARN, "too many conflict trans id", K(max_reserved_conflict_tx_num),
               K(conflict_txs_), K(conflict_tx));
  } else if (!is_contain(conflict_txs_, conflict_tx)) {
    if (OB_FAIL(conflict_txs_.push_back(conflict_tx))) {
      DETECT_LOG(WARN, "fail to push conflict tx to conflict_txs_",
                 K(ret), K(conflict_txs_), K(conflict_tx));
    }
  }
  return ret;
}

int ObTxDesc::merge_conflict_txs(const ObIArray<ObTransID> &conflict_txs)
{
  ObSpinLockGuard guard(lock_);
  return merge_conflict_txs_(conflict_txs);
}

int ObTxDesc::merge_conflict_txs_(const ObIArray<ObTransID> &conflict_txs)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  for (int64_t idx = 0; idx < conflict_txs.count() && OB_SUCC(tmp_ret); ++idx) {
    // Conflict tracking is diagnostic/deadlock metadata and must not fail normal execution.
    if (OB_TMP_FAIL(add_conflict_tx_(conflict_txs.at(idx)))) {
      DETECT_LOG(WARN, "fail to add conflict tx to conflict_txs_",
                 K(tmp_ret), K(conflict_txs), K(conflict_txs.at(idx)));
    }
  }
  return ret;
}

int ObTxDesc::get_write_state_copy(ObTxWriteState &write_state, bool &has_write_state) const
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  write_state = ObTxWriteState();
  has_write_state = false;
  if (has_write_state_) {
    write_state = write_state_;
    has_write_state = true;
  }
  return ret;
}

void ObTxDesc::reset_write_state()
{
  ObSpinLockGuard guard(lock_);
  has_write_state_ = false;
  write_state_ = ObTxWriteState();
  state_change_flags_.WRITE_STATE_CHANGED_ = true;
}

int ObTxDesc::assign_write_state(const ObTxWriteState &participant)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  write_state_ = participant;
  has_write_state_ = true;
  state_change_flags_.WRITE_STATE_CHANGED_ = true;
  return ret;
}

int ObTxDesc::fill_read_snapshot_write_state(ObTxReadSnapshot &snapshot) const
{
  if (has_write_state_ && !write_state_.is_clean()) {
    snapshot.mark_write_state();
  }
  return OB_SUCCESS;
}

int ObTxDesc::find_write_state_after(ObTxWriteState *&part, const ObTxSEQ scn)
{
  int ret = OB_SUCCESS;
  part = NULL;
  if (has_write_state_ && write_state_.last_scn_ > scn && !write_state_.is_clean()) {
    part = &write_state_;
  }
  return ret;
}

int ObTxDesc::get_abort_write_state(const ObTxWriteState *&part) const
{
  int ret = OB_SUCCESS;
  part = NULL;
  if (has_write_state_) {
    part = &write_state_;
  }
  return ret;
}

int ObTxDesc::get_savepoints_copy(ObTxSavePointList &copy_savepoints)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (OB_FAIL(copy_savepoints.assign(savepoints_))) {
    TRANS_LOG(WARN, "TxDesc get savepoints copy error", K(ret), KPC(this));
  }
  return ret;
}

ObTxParam::ObTxParam()
  : timeout_us_(0),
    lock_timeout_us_(-1),
    access_mode_(ObTxAccessMode::RW),
    isolation_(ObTxIsolationLevel::RC)
{}
bool ObTxParam::is_valid() const
{
  return timeout_us_ > 0
    && lock_timeout_us_ >= -1
    && access_mode_ != ObTxAccessMode::INVL
    && isolation_ != ObTxIsolationLevel::INVALID;
}
ObTxParam::~ObTxParam()
{
  timeout_us_ = 0;
  lock_timeout_us_ = -1;
  access_mode_ = ObTxAccessMode::INVL;
  isolation_ = ObTxIsolationLevel::INVALID;
}

ObTxSnapshot::ObTxSnapshot()
  : version_(), tx_id_(), scn_(), elr_(false) {}
ObTxSnapshot::ObTxSnapshot(const share::SCN &version)
  : version_(version), tx_id_(), scn_(), elr_(false) {}

ObTxSnapshot::~ObTxSnapshot()
{
  scn_.reset();
  elr_ = false;
}

void ObTxSnapshot::reset()
{
  version_.reset();
  tx_id_.reset();
  scn_.reset();
  elr_ = false;
}

ObTxSnapshot &ObTxSnapshot::operator=(const ObTxSnapshot &r)
{
  version_ = r.version_;
  tx_id_ = r.tx_id_;
  scn_ = r.scn_;
  elr_ = r.elr_;
  return *this;
}

ObTxReadSnapshot::ObTxReadSnapshot()
  : valid_(false),
    committed_(false),
    core_(),
    source_(SRC::INVL),
    uncertain_bound_(0),
    has_write_state_(false)
{}

ObTxReadSnapshot::~ObTxReadSnapshot()
{
  valid_ = false;
  committed_ = false;
  source_ = SRC::INVL;
  uncertain_bound_ = 0;
}

void ObTxReadSnapshot::reset()
{
  valid_ = false;
  committed_ = false;
  core_.reset();
  source_ = SRC::INVL;
  uncertain_bound_ = 0;
  has_write_state_ = false;
}

int ObTxReadSnapshot::assign(const ObTxReadSnapshot &from)
{
  int ret = OB_SUCCESS;
  valid_ = from.valid_;
  committed_ = from.committed_;
  core_ = from.core_;
  source_ = from.source_;
  uncertain_bound_ = from.uncertain_bound_;
  has_write_state_ = from.has_write_state_;
  return ret;
}

void ObTxReadSnapshot::reset_write_state()
{
  has_write_state_ = false;
}

void ObTxReadSnapshot::init_weak_read(const SCN snapshot)
{
  core_.version_ = snapshot;
  core_.tx_id_.reset();
  core_.scn_.reset();
  core_.elr_ = false;
  source_ = SRC::WEAK_READ_SERVICE;
  has_write_state_ = false;
  valid_ = true;
}


void ObTxReadSnapshot::init_ls_read(const ObTxSnapshot &core)
{
  core_ = core;
  source_ = SRC::LS;
  valid_ = true;
}

void ObTxReadSnapshot::specify_snapshot_scn(const share::SCN snapshot)
{
  core_.version_ = snapshot;
  source_ = SRC::SPECIAL;
}

void ObTxReadSnapshot::try_set_read_elr()
{
  const bool can_read_elr = source_ == SRC::LS;
  core_.set_elr(can_read_elr);
}


const char* ObTxReadSnapshot::get_source_name() const
{
  static const char* const SRC_NAME[] = { "INVALID", "GTS", "LOCAL", "WEAK_READ", "USER_SPECIFIED", "NONE" };
  return SRC_NAME[(int)source_];
}

ObTxExecResult::ObTxExecResult()
  : allocator_("TxExecResult"),
    incomplete_(false),
    touched_storage_(false),
    has_write_state_(false),
    write_state_()
{}

ObTxExecResult::~ObTxExecResult()
{
  incomplete_ = false;
}

void ObTxExecResult::reset()
{
  incomplete_ = false;
  touched_storage_ = false;
  has_write_state_ = false;
  write_state_ = ObTxWriteState();
  conflict_txs_.reset();
  allocator_.reset();
}

int ObTxExecResult::set_write_state(const ObTxWriteState &part)
{
  int ret = OB_SUCCESS;
  write_state_ = part;
  has_write_state_ = true;
  return ret;
}

int ObTxExecResult::merge_write_state(const ObTxWriteState &part, const bool has_write_state)
{
  int ret = OB_SUCCESS;
  if (has_write_state && OB_FAIL(set_write_state(part))) {
    incomplete_ = true;
    TRANS_LOG(WARN, "merge exec write state failed", K(ret), K(part), KPC(this));
  }
  return ret;
}

template<typename T>
static int append_dedup(ObIArray<T> &a, const ObIArray<T> &b)
{
  int ret = OB_SUCCESS;
  ARRAY_FOREACH(b, i) {
    if (!is_contain(a, b.at(i))) {
      ret = a.push_back(b.at(i));
    }
  }
  return ret;
}

int ObTxExecResult::merge_result(const ObTxExecResult &r)
{
  int ret = OB_SUCCESS;
  TRANS_LOG(TRACE, "txExecResult.merge with.start", K(r), KPC(this), K(lbt()));
  incomplete_ |= r.incomplete_;
  if (OB_FAIL(merge_write_state(r.write_state_, r.has_write_state_))) {
    incomplete_ = true;
    TRANS_LOG(WARN, "merge fail, set incomplete", K(ret), KPC(this));
  }
  touched_storage_ |= r.touched_storage_;
  if (OB_SUCC(ret)) {
    ret = merge_cflict_txs(r.conflict_txs_);
  }
  if (incomplete_) {
    TRANS_LOG(TRACE, "tx result incomplete:", KP(this));
  }

  TRANS_LOG(TRACE, "txExecResult.merge with.end", KPC(this));
  return ret;
}

int ObTxExecResult::merge_cflict_txs(
    const common::ObIArray<transaction::ObTransID> &txs)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_dedup(conflict_txs_, txs))) {
    DETECT_LOG(WARN, "append fail", KR(ret), KPC(this), K(txs));
  }
  return ret;
}

int ObTxExecResult::assign(const ObTxExecResult &r)
{
  int ret = OB_SUCCESS;
  incomplete_ = r.incomplete_;
  has_write_state_ = false;
  write_state_ = ObTxWriteState();
  if (OB_FAIL(merge_write_state(r.write_state_, r.has_write_state_))) {
    incomplete_ = true;
    TRANS_LOG(WARN, "assign fail, set incomplete", K(ret), KPC(this));
  }
  touched_storage_ = r.touched_storage_;
  conflict_txs_.assign(r.conflict_txs_);
  conflict_info_array_.assign(r.conflict_info_array_);
  return ret;
}

ObTxWriteState::ObTxWriteState()
  : first_scn_(),
    last_scn_(),
    last_touch_ts_(0),
    flag_()
{
}

ObTxWriteState::~ObTxWriteState()
{
  first_scn_.reset();
  last_scn_.reset();
  last_touch_ts_ = 0;
  flag_.reset();
}

int ObTxDescMgr::init(std::function<int(ObTransID&)> tx_id_allocator, const lib::ObMemAttr &mem_attr)
{
  int ret = OB_SUCCESS;
  OV(!inited_, OB_INIT_TWICE);
  OV(stoped_);
  OZ(map_.init(mem_attr));
  if (OB_SUCC(ret)) {
    tx_id_allocator_ = tx_id_allocator;
    inited_ = true;
    stoped_ = true;
  }
  int active_cnt = map_.alloc_cnt();
  TRANS_LOG(INFO, "txDescMgr.init", K(ret), K(inited_), K(stoped_), K(active_cnt));
  return ret;
}
int ObTxDescMgr::start()
{
  int ret = OB_SUCCESS;
  CK(inited_);
  CK(stoped_);
  OX(stoped_ = false);
  int active_cnt = map_.alloc_cnt();
  TRANS_LOG(INFO, "txDescMgr.start", K(inited_), K(stoped_), K(active_cnt));
  return ret;
}

class StopTxDescFunctor
{
public:
  StopTxDescFunctor(ObTransService &txs): txs_(txs) {}
  bool operator()(ObTxDesc *tx_desc)
  {
    int ret = OB_SUCCESS;
    if (OB_ISNULL(tx_desc) || !tx_desc->is_valid()) {
      TRANS_LOG(ERROR, "stop tx desc invalid argument", KPC(tx_desc));
    } else {
      TRANS_LOG(INFO, "stop tx desc", "tx_id", tx_desc->get_tx_id());
      if (OB_FAIL(txs_.stop_tx(*tx_desc))) {
        TRANS_LOG(ERROR, "stop tx desc fail", K(ret));
      } else {
        TRANS_LOG(INFO, "stop tx desc succeed");
      }
    }
    return true;
  }
  ObTransService &txs_;
};

class PrintTxDescFunctor
{
public:
  explicit PrintTxDescFunctor(const int64_t max_print_cnt) : max_print_cnt_(max_print_cnt) {}
  bool operator()(ObTxDesc *tx_desc)
  {
    bool bool_ret = false;
    if (OB_NOT_NULL(tx_desc) && max_print_cnt_-- > 0) {
      tx_desc->print_trace();
      bool_ret = true;
    }
    return bool_ret;
  }
  int64_t max_print_cnt_;
};

int ObTxDescMgr::stop()
{
  int ret = OB_SUCCESS;

  stoped_ = true;
  int active_cnt = map_.alloc_cnt();

  StopTxDescFunctor fn(txs_);
  if (OB_FAIL(map_.for_each(fn))) {
    TRANS_LOG(ERROR, "for each transaction desc error", KR(ret));
  }
  TRANS_LOG(INFO, "txDescMgr.stop", K(inited_), K(stoped_), K(active_cnt));
  return OB_SUCCESS;
}

int ObTxDescMgr::wait()
{
  int ret = OB_SUCCESS;
  const int64_t SLEEP_US = 100 * 1000;
  const int64_t MAX_RETRY_TIMES = 50;
  int active_cnt = 0;
  if (inited_) {
    int i = 0;
    bool done = false;
    while (!done && i++ < MAX_RETRY_TIMES) {
      active_cnt = map_.alloc_cnt();
      if (!active_cnt) {
        TRANS_LOG(INFO, "txDescMgr.wait done.");
        done = true;
        break;
      }
      TRANS_LOG(WARN, "txDescMgr.waiting.", K(active_cnt));
      ob_usleep(SLEEP_US);
    }
    if (!done) {
      ret = OB_TIMEOUT;
      TRANS_LOG(WARN, "txDescMgr.wait timeout", K(ret));
      PrintTxDescFunctor fn(128);
#ifdef ENABLE_DEBUG_LOG
      (void)map_.alloc_handle_.for_each(fn);
#else
      (void)map_.for_each(fn);
#endif
    }
  }
  TRANS_LOG(INFO, "txDescMgr.wait", K(ret), K(inited_), K(stoped_), K(active_cnt));
  return ret;
}

void ObTxDescMgr::destroy() { inited_ = false; }
int ObTxDescMgr::alloc(ObTxDesc *&tx_desc)
{
  int ret = OB_SUCCESS;
  OV(inited_, OB_NOT_INIT);
  OV(!stoped_, OB_IN_STOP_STATE);
  OZ(map_.alloc_value(tx_desc));
  OX(tx_desc->inc_ref(1));
  return ret;
}
void ObTxDescMgr::free(ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  OV(inited_, OB_NOT_INIT);
  OX(map_.free_value(tx_desc));
}
int ObTxDescMgr::add(ObTxDesc &tx_desc)
{
  int ret = OB_SUCCESS;
  ObTransID tx_id;
  OV(inited_, OB_NOT_INIT);
  OV(!stoped_, OB_IN_STOP_STATE);
  CK(!tx_desc.get_tx_id().is_valid());
  OZ(tx_id_allocator_(tx_id));
  // set_tx_id should before insert_and_get
  OX(tx_desc.set_tx_id(tx_id));
  OZ(map_.insert(tx_id, &tx_desc), tx_desc);
  // if fail revert tx_desc.tx_id_ member
  if (OB_FAIL(ret) && tx_id.is_valid()) {
    tx_desc.reset_tx_id();
  }
  OX(tx_desc.flags_.SHADOW_ = false);
  TRANS_LOG(TRACE, "txDescMgr.register trans", K(ret), K(tx_id), K(tx_desc));
  return ret;
}

int ObTxDescMgr::get(const ObTransID &tx_id, ObTxDesc *&tx_desc)
{
  int ret = OB_SUCCESS;
  OV(inited_, OB_NOT_INIT);
  if (OB_SUCC(ret)) {
    ret = map_.get(tx_id, tx_desc);
  }
  TRANS_LOG(TRACE, "txDescMgr.get trans", K(tx_id), KP(tx_desc));
  return ret;
}

void ObTxDescMgr::revert(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  ObTransID tx_id = tx.get_tx_id();
  OV(inited_, OB_NOT_INIT);
  if (OB_SUCC(ret)) {
    map_.revert(&tx);
  }
  // tx_id may be invalid when tx was reused before.
  TRANS_LOG(TRACE, "txDescMgr.revert trans", K(tx_id), KP(&tx));
}

int ObTxDescMgr::remove(ObTxDesc &tx)
{
  int ret = OB_SUCCESS;
  ObTransID tx_id = tx.get_tx_id();
  TRANS_LOG(TRACE, "txDescMgr.unregister trans:", K(tx_id), KP(&tx));
  OV(inited_, OB_NOT_INIT);
  OX(map_.del(tx_id, &tx));
  OX(tx.flags_.SHADOW_ = true);
  return ret;
}

int ObTxDescMgr::acquire_tx_ref(const ObTransID &trans_id)
{
  int ret = OB_SUCCESS;
  ObTxDesc *tx_desc = nullptr;
  CK(trans_id.is_valid());
  OZ(get(trans_id, tx_desc), trans_id);
  LOG_TRACE("txDescMgr.acquire tx ref", K(ret), K(trans_id), KP(tx_desc));
  return ret;
}

int ObTxDescMgr::release_tx_ref(ObTxDesc *tx_desc)
{
  int ret = OB_SUCCESS;
  CK(OB_NOT_NULL(tx_desc));
  OX(revert(*tx_desc));
  LOG_TRACE("txDescMgr.release tx ref", K(ret), KP(tx_desc));
  return ret;
}

int ObTxDescMgr::iterate_tx_scheduler_stat(ObTxSchedulerStatIterator &tx_scheduler_stat_iter)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    TRANS_LOG(WARN, "ObTxDescMgr not inited");
    ret = OB_NOT_INIT;
  } else {
    IterateTxSchedulerFunctor fn(tx_scheduler_stat_iter);
    if (OB_FAIL(map_.for_each(fn))) {
      TRANS_LOG(WARN, "for each transaction scheduler error", KR(ret));
    }
  }

  return ret;
}

int ObTxDesc::alloc_branch_id(const int64_t count, int16_t &branch_id)
{
  int ret = OB_SUCCESS;
  ObSpinLockGuard guard(lock_);
  if (count > MAX_BRANCH_ID_VALUE - last_branch_id_) {
    ret = OB_ERR_OUT_OF_UPPER_BOUND;
    TRANS_LOG(WARN, "can not alloc branch_id", KR(ret), K(count), KPC(this));
  } else {
    branch_id = last_branch_id_ + 1;
    last_branch_id_ += count;
  }
  return ret;
}
void ObTxDesc::mark_write_state_aborted(const ObTransID tx_id, const int abort_cause)
{
  ObSpinLockGuard guard(lock_);
  if (tx_id == tx_id_ && state_ < State::IN_TERMINATE && !flags_.WRITE_STATE_ABORTED_) {
    flags_.WRITE_STATE_ABORTED_ = true;
    abort_cause_ = abort_cause;
  }
}

// 1. clear transaction level snapshot
// 2. clear savepoints
int ObTxDesc::clear_state_for_autocommit_retry()
{
  ObSpinLockGuard guard(lock_);
  if (tx_id_.is_valid()) {
    savepoints_.reset();
    if (with_tx_snapshot()) {
      snapshot_version_.reset();
      snapshot_scn_.reset();
      snapshot_uncertain_bound_ = 0;
      TRANS_LOG(TRACE, "", KPC(this));
    }
  }
  return OB_SUCCESS;
}

bool ObTxDesc::is_write_state_clean() const
{
  return !has_write_state_ || write_state_.is_clean();
}

bool ObTxDesc::is_write_state_without_valid_write() const
{
  return !has_write_state_
         || write_state_.is_clean()
         || write_state_.is_without_valid_write();
}
} // transaction
} // oceanbase
#undef USING_LOG_PREFIX
