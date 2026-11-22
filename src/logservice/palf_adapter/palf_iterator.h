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

#ifndef OCEANBASE_LOGSERVICE_PALF_ADAPTER_ITERATOR_
#define OCEANBASE_LOGSERVICE_PALF_ADAPTER_ITERATOR_

#include <optional>
#include <type_traits>
#include "lib/function/ob_function.h"
#include "rust/palf.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace palf
{
struct PayloadEntry {};
struct GroupEntry {};

typedef common::ObFunction<void()> DestroyStorageFunctor;

inline int from_rust_code(const palf_rs::Code code)
{
  switch (code) {
    case palf_rs::Code::Ok: return OB_SUCCESS;
    case palf_rs::Code::Invalid: return OB_INVALID_ARGUMENT;
    case palf_rs::Code::NotInit: return OB_NOT_INIT;
    case palf_rs::Code::End: return OB_ITER_END;
    case palf_rs::Code::OutLower: return OB_ERR_OUT_OF_LOWER_BOUND;
    case palf_rs::Code::OutUpper: return OB_ERR_OUT_OF_UPPER_BOUND;
    case palf_rs::Code::Partial: return common::OB_PARTIAL_LOG;
    case palf_rs::Code::Duplicate: return OB_ENTRY_EXIST;
    case palf_rs::Code::Corrupt:
    case palf_rs::Code::UnknownVersion: return OB_INVALID_DATA;
    case palf_rs::Code::Io: return OB_IO_ERROR;
    case palf_rs::Code::NoSpace: return common::OB_LOG_OUTOF_DISK_SPACE;
    case palf_rs::Code::NoMemory: return OB_ALLOCATE_MEMORY_FAILED;
    case palf_rs::Code::Closed: return OB_NOT_RUNNING;
    default: return OB_ERR_UNEXPECTED;
  }
}

inline share::SCN from_rust_scn(const uint64_t value)
{
  share::SCN scn;
  (void)scn.convert_for_logservice(value);
  return scn;
}

template <class EntryKind>
class PalfIterator
{
public:
  PalfIterator() = default;
  ~PalfIterator() { destroy(); }

  int init(::rust::Box<palf_rs::Cursor> cursor)
  {
    int ret = OB_SUCCESS;
    if (cursor_.has_value()) {
      ret = OB_INIT_TWICE;
    } else {
      cursor_.emplace(std::move(cursor));
      is_inited_ = true;
    }
    return ret;
  }

  int reuse(const LSN &start_lsn)
  {
    int ret = OB_SUCCESS;
    if (!is_inited_ || !start_lsn.is_valid()) {
      ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
    } else {
      ret = from_rust_code(palf_rs::reuse_ffi(**cursor_, start_lsn.val_));
      has_entry_ = false;
    }
    return ret;
  }

  void destroy()
  {
    cursor_.reset();
    has_entry_ = false;
    is_inited_ = false;
    if (destroy_storage_functor_.is_valid()) {
      destroy_storage_functor_();
      destroy_storage_functor_.reset();
    }
  }

  int next()
  {
    return next(share::SCN::max_scn());
  }

  int next(const share::SCN &replayable_point_scn)
  {
    share::SCN next_min_scn;
    bool iterate_end_by_replayable_point = false;
    return next(replayable_point_scn, next_min_scn, iterate_end_by_replayable_point);
  }

  int next(const share::SCN &replayable_point_scn,
           share::SCN &next_min_scn,
           bool &iterate_end_by_replayable_point)
  {
    int ret = OB_SUCCESS;
    next_min_scn.reset();
    iterate_end_by_replayable_point = false;
    if (!is_inited_ || !replayable_point_scn.is_valid()) {
      ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
    } else {
      ret = from_rust_code(palf_rs::next_ffi(**cursor_, replayable_point_scn.get_val_for_logservice()));
      has_entry_ = OB_SUCCESS == ret;
      if (OB_ITER_END == ret) {
        const uint64_t next_scn = palf_rs::next_min_scn(**cursor_);
        if (0 != next_scn) {
          next_min_scn = from_rust_scn(next_scn);
          iterate_end_by_replayable_point = true;
        }
      }
    }
    return ret;
  }

  int get_entry(const char *&buffer, int64_t &nbytes, share::SCN &scn, LSN &lsn)
  {
    int ret = OB_SUCCESS;
    if (!is_inited_ || !has_entry_) {
      ret = is_inited_ ? OB_ITER_END : OB_NOT_INIT;
    } else {
      constexpr bool group_mode = std::is_same<EntryKind, GroupEntry>::value;
      const auto data = group_mode ? palf_rs::bytes(**cursor_) : palf_rs::payload(**cursor_);
      buffer = reinterpret_cast<const char *>(data.data());
      nbytes = data.size();
      scn = from_rust_scn(palf_rs::scn(**cursor_));
      lsn = LSN(palf_rs::lsn(**cursor_));
    }
    return ret;
  }

  LSN get_entry_end_lsn() const
  {
    return is_valid() ? LSN(palf_rs::end_lsn(**cursor_)) : LSN();
  }

  bool is_inited() const { return is_inited_; }
  bool is_valid() const { return is_inited_ && cursor_.has_value() && has_entry_; }
  bool check_is_the_last_entry()
  {
    return is_valid() && palf_rs::is_last_ffi(**cursor_);
  }
  int set_destroy_iterator_storage_functor(const DestroyStorageFunctor &destroy_func)
  {
    int ret = OB_SUCCESS;
    if (!is_inited_ || !destroy_func.is_valid()) {
      ret = is_inited_ ? OB_INVALID_ARGUMENT : OB_NOT_INIT;
    } else {
      destroy_storage_functor_ = destroy_func;
      if (!destroy_storage_functor_.is_valid()) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    return ret;
  }

  TO_STRING_KV(K_(is_inited), K_(has_entry));

private:
  std::optional<::rust::Box<palf_rs::Cursor>> cursor_;
  DestroyStorageFunctor destroy_storage_functor_;
  bool has_entry_ = false;
  bool is_inited_ = false;
};

typedef PalfIterator<PayloadEntry> MemPalfBufferIterator;
typedef PalfIterator<GroupEntry> MemPalfGroupBufferIterator;
typedef PalfIterator<PayloadEntry> PalfBufferIterator;
typedef PalfIterator<GroupEntry> PalfGroupBufferIterator;
} // namespace palf
} // namespace oceanbase
#endif
