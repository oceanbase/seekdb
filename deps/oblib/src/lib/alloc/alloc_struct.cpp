/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "lib/alloc/alloc_struct.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{

using namespace common;

namespace lib
{
static bool g_memleak_light_backtrace_enabled = false;

ObMallocHookAttrGuard::ObMallocHookAttrGuard(const ObMemAttr& attr, const bool use_500)
  : old_attr_(get_tl_mem_attr()), old_use_500_(get_tl_use_500())
{
  get_tl_mem_attr() = attr;
  get_tl_mem_attr().ctx_id_ = ObCtxIds::GLIBC;
  get_tl_use_500() = use_500;
}

ObMallocHookAttrGuard::~ObMallocHookAttrGuard()
{
  get_tl_mem_attr() = old_attr_;
  get_tl_use_500() = old_use_500_;
}

bool ObLabel::operator==(const ObLabel &other) const
{
  bool bret = false;
  if (is_valid() && other.is_valid()) {
    if (str_[0] == other.str_[0]) {
      if (0 == STRCMP(str_, other.str_)) {
        bret = true;
      }
    }
  } else if (!is_valid() && !other.is_valid()) {
    bret = true;
  }
  return bret;
}

ObLabel::operator const char *() const
{
  return str_;
}

int64_t ObLabel::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  (void)common::logdata_printf(
      buf, buf_len, pos, "%s", (const char*)(*this));
  return pos;
}

int64_t ObMemAttr::to_string(char* buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  (void)common::logdata_printf(
      buf, buf_len, pos,
      "tenant_id=%ld, label=%s, ctx_id=%ld, prio=%d",
      tenant_id_, (const char *)label_, ctx_id_, prio_);
  return pos;
}

void Label::fmt(char *buf, int64_t buf_len, int64_t &pos, const char *str)
{
  if (OB_UNLIKELY(pos >= buf_len)) {
  } else {
    int64_t len = snprintf(buf + pos, buf_len - pos, "%s", str);
    if (len < buf_len - pos) {
      pos += len;
    } else {
      pos = buf_len;
    }
  }
}
int64_t ObUnmanagedMemoryStat::get_total_hold()
{
  int64_t total_hold = 0;
  for (int i = 0; i < N; i++) {
    total_hold += stat_[i].inc_hold_ - stat_[i].dec_hold_;
  }
  return total_hold;
}

void ObUnmanagedMemoryStat::inc(const int64_t size)
{
  int ps = get_page_size();
  int64_t hold = align_up(size, ps);
  int idx = 64 - __builtin_clzll(hold/ps);
  __sync_fetch_and_add(&stat_[idx].inc_hold_, hold);
  __sync_fetch_and_add(&stat_[idx].inc_size_, size);
  __sync_fetch_and_add(&stat_[idx].inc_cnt_, 1);
}

void ObUnmanagedMemoryStat::dec(const int64_t size)
{
  int ps = get_page_size();
  int hold = align_up(size, ps);
  int idx = 64 - __builtin_clzll(hold/ps);
  __sync_fetch_and_add(&stat_[idx].dec_hold_, hold);
  __sync_fetch_and_add(&stat_[idx].dec_size_, size);
  __sync_fetch_and_add(&stat_[idx].dec_cnt_, 1);
}

int ObUnmanagedMemoryStat::format_dist(char *buf, int64_t buf_len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int ps = get_page_size();
  ret = databuff_printf(buf, buf_len, pos, "{");
  for (int i = 0; OB_SUCC(ret) && i < N; i++) {
    Stat &stat = stat_[i];
    if (stat.inc_size_ || stat.dec_size_) {
      ret = databuff_printf(buf, buf_len, pos, "[%d:%ld-%ld-%ld:%ld-%ld-%ld]", i,
                            stat.inc_hold_, stat.inc_size_, stat.inc_cnt_,
                            stat.dec_hold_, stat.dec_size_, stat.dec_cnt_);
    }
  }
  if (OB_SUCC(ret)) {
    ret = databuff_printf(buf, buf_len, pos, "}");
  }
  return ret;
}

int64_t get_unmanaged_memory_size()
{
  return UNMAMAGED_MEMORY_STAT.get_total_hold();
}

void enable_memleak_light_backtrace(const bool enable)
{
#if defined(__x86_64__) || defined(__aarch64__)
  g_memleak_light_backtrace_enabled = enable;
#else
  UNUSED(enable);
#endif
}
bool is_memleak_light_backtrace_enabled()
{
  return g_memleak_light_backtrace_enabled;
}
} // end of namespace lib
} // end of namespace oceanbase
