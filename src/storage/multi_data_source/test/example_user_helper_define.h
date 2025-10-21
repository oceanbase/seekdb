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
 
#ifndef UNITTEST_SHARE_MULTI_DATA_SOURCE_EXAMPLE_USER_HELPER_DEFINE_H
#define UNITTEST_SHARE_MULTI_DATA_SOURCE_EXAMPLE_USER_HELPER_DEFINE_H
#include "storage/multi_data_source/buffer_ctx.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/serialization.h"
#include "share/scn.h"
#include "storage/multi_data_source/runtime_utility/common_define.h"
#include "deps/oblib/src/common/meta_programming/ob_type_traits.h"

namespace oceanbase {
namespace unittest {

struct ExampleUserHelperFunction1 {
  static int on_register(const char* buf,
                         const int64_t len,
                         storage::mds::BufferCtx &ctx); // out parameter, will record corresponding modifications in Ctx

  static int on_replay(const char* buf,
                       const int64_t len,
                       const share::SCN &scn, // log scn
                       storage::mds::BufferCtx &ctx); // standby replay
  static bool check_can_do_tx_end(const bool is_willing_to_commit,
                                  const bool for_replay,
                                  const share::SCN &log_scn,
                                  const char *buf,
                                  const int64_t buf_len,
                                  storage::mds::BufferCtx &ctx,
                                  const char *&can_not_do_reason);
};

struct ExampleUserHelperFunction2 {
  static int on_register(const char* buf,
                         const int64_t len,
                         storage::mds::BufferCtx &ctx); // out parameter, will record corresponding modifications in Ctx

  static int on_replay(const char* buf,
                       const int64_t len,
                       const share::SCN &scn, // log scn
                       storage::mds::BufferCtx &ctx); // standby replay
};

struct ExampleUserHelperFunction3 {
  static int on_register(const char* buf,
                         const int64_t len,
                         storage::mds::BufferCtx &ctx); // out parameter, will record corresponding modifications in Ctx

  static int on_replay(const char* buf,
                       const int64_t len,
                       const share::SCN &scn, // log scn
                       storage::mds::BufferCtx &ctx); // standby replay
  static bool check_can_do_on_prepare(bool for_replay,
                                      const char *buf,
                                      const int64_t buf_len,
                                      const share::SCN &scn,
                                      storage::mds::BufferCtx &ctx,
                                      const char *can_not_do_reason) {
    UNUSED(buf);
    UNUSED(buf_len);
    UNUSED(scn);
    UNUSED(ctx);
    bool ret = false;
    if (!for_replay) {
      ret = true;
      MDS_LOG(INFO, "[UNITTEST] test can not do prepare return true");
    } else {
      static int call_times = 0;
      if (call_times++ >= 1) {
        ret = true;
      } else {
        can_not_do_reason = "TEST CAN NOT DO ON_PREPARE";
      }
    }
    return ret;
  }
};

struct ExampleUserHelperCtx : public storage::mds::BufferCtx {
  ExampleUserHelperCtx() : call_times_(0) {}
  virtual const storage::mds::MdsWriter get_writer() const override;
  virtual void on_redo(const share::SCN &redo_scn) override;
  virtual void before_prepare() override;
  virtual void on_prepare(const share::SCN &prepare_version) override;
  virtual void on_commit(const share::SCN &commit_version, const share::SCN &scn) override;
  virtual void on_abort(const share::SCN &scn) override;
  int assign(const ExampleUserHelperCtx &rhs) {
    call_times_ = rhs.call_times_;
    return OB_SUCCESS;
  }
  int call_times_;// This class can have its own internal state
  virtual int64_t to_string(char*, const int64_t buf_len) const { return 0; }
  // Persist and restore along with transaction status
  virtual int serialize(char*, const int64_t, int64_t&) const { return OB_SUCCESS; }
  virtual int deserialize(const char*, const int64_t, int64_t&) { return OB_SUCCESS; }
  virtual int64_t get_serialize_size(void) const { return 0; }
};

#ifndef TEST_MDS_TRANSACTION

inline int ExampleUserHelperFunction1::on_register(const char* buf,
                                            const int64_t len,
                                            storage::mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline int ExampleUserHelperFunction1::on_replay(const char* buf,
                                        const int64_t len,
                                        const share::SCN &scn, // log scn
                                        storage::mds::BufferCtx &ctx)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline bool ExampleUserHelperFunction1::check_can_do_tx_end(const bool is_willing_to_commit,
                                                            const bool for_replay,
                                                            const share::SCN &log_scn,
                                                            const char *buf,
                                                            const int64_t buf_len,
                                                            storage::mds::BufferCtx &ctx,
                                                            const char *&can_not_do_reason)
{
  static_assert(OB_TRAIT_HAS_CHECK_CAN_DO_TX_END(ExampleUserHelperFunction1), "static check failed");
  bool ret = true;
  UNUSED(is_willing_to_commit);
  UNUSED(for_replay);
  UNUSED(log_scn);
  UNUSED(buf);
  UNUSED(buf_len);
  UNUSED(ctx);
  static int call_times = 0;
  if (call_times++ < 5) {
    ret = false;
    can_not_do_reason = "JUST FOR TEST";
  }
  return ret;
}

inline int ExampleUserHelperFunction2::on_register(const char*,
                                            const int64_t,
                                            storage::mds::BufferCtx &)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline int ExampleUserHelperFunction2::on_replay(const char*,
                                        const int64_t,
                                        const share::SCN &, // log scn
                                        storage::mds::BufferCtx &)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline int ExampleUserHelperFunction3::on_register(const char*,
                                            const int64_t,
                                            storage::mds::BufferCtx &)
{
  int ret = OB_SUCCESS;
  return ret;
}

inline int ExampleUserHelperFunction3::on_replay(const char* buf,
                                        const int64_t len,
                                        const share::SCN &scn, // log scn
                                        storage::mds::BufferCtx &ctx)
{
  UNUSED(scn);
  return on_register(buf, len, ctx);
}

inline const storage::mds::MdsWriter ExampleUserHelperCtx::get_writer() const
{
  return storage::mds::MdsWriter(storage::mds::WriterType::TRANSACTION, 1);
}

inline void ExampleUserHelperCtx::on_redo(const share::SCN &redo_scn)
{
  MDS_LOG(INFO, "[UNITTEST] call on_redo with ctx", K(++call_times_));
}

inline void ExampleUserHelperCtx::before_prepare()
{
  MDS_LOG(INFO, "[UNITTEST] call before_prepare with ctx", K(++call_times_));
}

inline void ExampleUserHelperCtx::on_prepare(const share::SCN &prepare_version)
{
  MDS_LOG(INFO, "[UNITTEST] call on_prepare with ctx", K(++call_times_));
}

inline void ExampleUserHelperCtx::on_commit(const share::SCN &commit_version, const share::SCN &scn)
{
  MDS_LOG(INFO, "[UNITTEST] call on_commit with ctx", K(++call_times_));
}

inline void ExampleUserHelperCtx::on_abort(const share::SCN &scn)
{
  MDS_LOG(INFO, "[UNITTEST] call on_abort with ctx", K(++call_times_));
}
#endif

}
}
#endif
