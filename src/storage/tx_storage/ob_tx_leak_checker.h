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
#ifndef OCEANBASE_STORAGE_OB_TX_LEAK_CHECKER_H_
#define OCEANBASE_STORAGE_OB_TX_LEAK_CHECKER_H_
#include "share/leak_checker/ob_leak_checker.h"
#include "share/rc/ob_module_provider.h"
#include "common/ob_tablet_id.h"
#include "lib/profile/ob_trace_id.h"
#include "storage/ob_common_id_utils.h"
namespace oceanbase
{
namespace storage
{

struct ObReadOnlyTxCheckerKey
{
public:
  ObReadOnlyTxCheckerKey()
    : seq_(0)
  {}
  ~ObReadOnlyTxCheckerKey() = default;
  int hash(uint64_t &hash_value) const
  {
    hash_value = seq_;
    return OB_SUCCESS;
  }
  OB_INLINE bool is_valid() const
  {
    return (seq_ != 0);
  }
  bool operator== (const ObReadOnlyTxCheckerKey &other) const
  {
    return (true
            && seq_ == other.seq_);
  }
  TO_STRING_KV(K_(seq));
public:
  
  int64_t seq_;
};

struct ObReadExInfo
{
public:
  enum {
    INVALID_TYPE = 0,
    WITH_PLAN    = 1,
    WITH_TRACE   = 2,
    WITH_LBT     = 3
  };
  ObReadExInfo() : type_(INVALID_TYPE) {}
  ~ObReadExInfo() {}
  TO_STRING_KV(K_(type));
public:
  int64_t type_;
};

struct ObReadExInfoPlan : public ObReadExInfo
{
public:
  ObReadExInfoPlan()
    : plan_id_(0)
  { type_ = WITH_PLAN; }
  ~ObReadExInfoPlan() {}
  INHERIT_TO_STRING_KV("ObReadExInfo", ObReadExInfo, K_(plan_id));
public:
  int64_t plan_id_;
};

struct ObReadExInfoTrace : public ObReadExInfoPlan
{
public:
  ObReadExInfoTrace()
    : trace_id_()
  { type_ = WITH_TRACE; }
  ~ObReadExInfoTrace() {}
  INHERIT_TO_STRING_KV("ObReadExInfoPlan", ObReadExInfoPlan, K_(trace_id));
public:
  common::ObCurTraceId::TraceId trace_id_;
};

struct ObReadExInfoBT : public ObReadExInfoTrace
{
public:
  ObReadExInfoBT() { type_ = WITH_LBT; bt_[0] = '\0'; }
  ~ObReadExInfoBT() {}
  INHERIT_TO_STRING_KV("ObReadExInfoTrace", ObReadExInfoTrace, K_(bt));
public:
  char bt_[512];
};

struct ObReadOnlyTxCheckerValue
{
public:
  ObReadOnlyTxCheckerValue()
    : timestamp_(0),
      tablet_id_(),
      extra_(nullptr)
  {
  }
  ~ObReadOnlyTxCheckerValue() = default;
  TO_STRING_KV(K_(timestamp), K_(tablet_id), KPC_(extra));
public:
  
  int64_t timestamp_;
  common::ObTabletID tablet_id_;
  ObReadExInfo *extra_;
};

struct ObReadOnlyTxPrinter
{
  bool operator()(const ObReadOnlyTxCheckerKey &k, const ObReadOnlyTxCheckerValue &v)
  {
    bool ret = true;
    if (OB_ISNULL(v.extra_)) {
      COMMON_LOG(INFO, "LEAK_CHECKER ",
                 "key", k,
                 "value", v);
    } else if (OB_NOT_NULL(v.extra_)) {
      if (v.extra_->type_ >= ObReadExInfo::WITH_LBT) {
        ObReadExInfoBT *extra_info = static_cast<ObReadExInfoBT *>(v.extra_);
        COMMON_LOG(INFO, "LEAK_CHECKER ",
               "key", k,
               "value", v,
               KPC(extra_info));
      } else if (v.extra_->type_ >= ObReadExInfo::WITH_TRACE) {
        ObReadExInfoTrace *extra_info = static_cast<ObReadExInfoTrace *>(v.extra_);
        COMMON_LOG(INFO, "LEAK_CHECKER ",
               "key", k,
               "value", v,
               KPC(extra_info));
      } else if (v.extra_->type_ >= ObReadExInfo::WITH_PLAN) {
        ObReadExInfoPlan *extra_info = static_cast<ObReadExInfoPlan *>(v.extra_);
        COMMON_LOG(INFO, "LEAK_CHECKER ",
               "key", k,
               "value", v,
               KPC(extra_info));
      }
    }
    return ret;
  }
};

struct ObReadOnlyTxDiagnoseFunctor
{
  ObReadOnlyTxDiagnoseFunctor(char *buf,
                              const int64_t pos,
                              const int64_t len)
    : buf_(buf), pos_(pos), len_(len)
  {}
  ~ObReadOnlyTxDiagnoseFunctor() {}
  bool operator()(const ObReadOnlyTxCheckerKey &k, const ObReadOnlyTxCheckerValue &v)
  {
    UNUSED(k);
    bool bool_ret = true;
    int ret = OB_SUCCESS;
    if (OB_FAIL(databuff_printf(buf_, len_, pos_, "{tablet_id:%ld",
                                v.tablet_id_.id()))){
      bool_ret = false;
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(v.extra_)) {
      if (OB_SUCC(ret) && v.extra_->type_ >= ObReadExInfo::WITH_PLAN) {
        ObReadExInfoPlan *info = static_cast<ObReadExInfoPlan *>(v.extra_);
        if (OB_FAIL(databuff_printf(buf_, len_, pos_, ", plan_id:%ld",
                                    info->plan_id_))){
          bool_ret = false;
        }
      }
      if (OB_SUCC(ret) && v.extra_->type_ >= ObReadExInfo::WITH_TRACE) {
        ObReadExInfoTrace *info = static_cast<ObReadExInfoTrace *>(v.extra_);
        if (OB_FAIL(databuff_print_multi_objs(buf_, len_, pos_, ", trace:",
                                    info->trace_id_))){
          bool_ret = false;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(databuff_printf(buf_, len_, pos_, "}, "))){
        bool_ret = false;
      }
    }
    return bool_ret;
  }
public:
  char *buf_;
  int64_t pos_;
  int64_t len_;
};

const static int64_t TX_DEBUG_LEVEL_CACHE_REFRESH_INTERVAL = 1_s;
static int64_t get_tx_debug_level()
{
  RLOCAL_INIT(int64_t, last_check_timestamp, 0);
  RLOCAL_INIT(int64_t, last_result, 0);
  int64_t current_time = ObClockGenerator::getClock();
  if (current_time - last_check_timestamp < TX_DEBUG_LEVEL_CACHE_REFRESH_INTERVAL) {
    // Reuse the cached value until the refresh interval expires.
  } else {
    last_result = GCONF._tx_debug_level;
    last_check_timestamp = current_time;
  }

  return last_result;
}

typedef share::ObBaseLeakChecker<ObReadOnlyTxCheckerKey, ObReadOnlyTxCheckerValue> ObReadOnlyTxChecker;
#define READ_CHECKER_RECORD(ctx)                                                         \
  do {                                                                                   \
    const static int64_t MAX_RECORD_CNT = 100000;    /* 10W * 0.5K = 50MB */             \
    int64_t tx_debug_level = get_tx_debug_level();                                       \
    if (OB_LIKELY(tx_debug_level <= 0)) {                                                \
    } else {                                                                             \
      ObReadOnlyTxCheckerKey key;                                                        \
      ObReadOnlyTxCheckerValue value;                                                    \
      key.seq_ = share::g_mp->trans_service()->get_unique_seq();                                \
      value.timestamp_ = ObClockGenerator::getClock();                                   \
      value.tablet_id_ = ctx.tablet_id_;                                                 \
      ctx.check_seq_ = key.seq_;                                                         \
      if (OB_UNLIKELY(tx_debug_level >= 4)) {                                            \
        void* buf = ob_malloc(sizeof(ObReadExInfoBT), ObMemAttr("readleakchecker")); \
        if (OB_NOT_NULL(buf)) {                                                                \
          ObReadExInfoBT *extra_info = new(buf) ObReadExInfoBT();                              \
          extra_info->trace_id_ = *(ObCurTraceId::get_trace_id());                             \
          lbt(extra_info->bt_, sizeof(extra_info->bt_));                                       \
          value.extra_ = extra_info;                                                           \
        }                                                                                      \
      } else if (OB_UNLIKELY(tx_debug_level >= 3)) {                                              \
        void* buf = ob_malloc(sizeof(ObReadExInfoTrace), ObMemAttr("readleakchecker")); \
        if (OB_NOT_NULL(buf)) {                                                                   \
          ObReadExInfoTrace *extra_info = new(buf) ObReadExInfoTrace();                           \
          extra_info->trace_id_ = *(ObCurTraceId::get_trace_id());                                \
          value.extra_ = extra_info;                                                              \
        }                                                                                         \
      } else if (OB_UNLIKELY(tx_debug_level >= 2)) {                                              \
        void* buf = ob_malloc(sizeof(ObReadExInfoPlan), ObMemAttr("readleakchecker"));  \
        if (OB_NOT_NULL(buf)) {                                                                   \
          ObReadExInfoPlan *extra_info = new(buf) ObReadExInfoPlan();                             \
          value.extra_ = extra_info;                                                              \
        }                                                                                         \
      } else if (OB_LIKELY(tx_debug_level >= 1)) {                                                \
      }                                                                                           \
      share::g_mp->trans_service()->get_read_tx_checker().record(key, value, MAX_RECORD_CNT);            \
    }                                                                                             \
  } while(0)

#define READ_CHECKER_RELEASE(ctx)                                                                \
  do {                                                                                           \
    if (OB_UNLIKELY(!share::g_mp->trans_service()->get_read_tx_checker().is_empty())) {                 \
      ObReadOnlyTxCheckerKey key;                                                                \
      ObReadOnlyTxCheckerValue value;                                                            \
      key.seq_ = ctx.check_seq_;                                                                 \
      share::g_mp->trans_service()->get_read_tx_checker().release(key, value);                          \
      if (OB_NOT_NULL(value.extra_)) {                                                           \
        ob_free(value.extra_);                                                                   \
      }                                                                                          \
    }                                                                                            \
  } while(0)

#define READ_CHECKER_PRINT() \
  do {                            \
    ObReadOnlyTxPrinter fn;       \
    share::g_mp->trans_service()->get_read_tx_checker().for_each(fn); \
  } while(0)

#define READ_CHECKER_FOR_EACH(fn)                              \
  do {                                                         \
    share::g_mp->trans_service()->get_read_tx_checker().for_each(fn); \
  } while(0)

}  // storage
}  // oceanbase

#endif  // OCEANBASE_STORAGE_OB_TX_LEAK_CHECKER_H_
