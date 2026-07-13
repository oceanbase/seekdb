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

#ifndef OCEANBASE_TRANSACTION_OB_TS_MGR_
#define OCEANBASE_TRANSACTION_OB_TS_MGR_

#include "ob_trans_define.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace transaction
{

class ObTsMgr
{
public:
  ObTsMgr() = default;
  virtual ~ObTsMgr() = default;

  virtual int get_gts(share::SCN &scn);
  virtual int get_gts(const MonotonicTs stc,
                      share::SCN &scn,
                      MonotonicTs &receive_gts_ts);
  virtual int get_gts_sync(const int64_t timeout_us, share::SCN &scn);
  virtual int wait_gts_elapse(const share::SCN &scn);

  static ObTsMgr &get_instance();
};

#define OB_TS_MGR (::oceanbase::transaction::ObTsMgr::get_instance())

} // namespace transaction
} // namespace oceanbase

#endif // OCEANBASE_TRANSACTION_OB_TS_MGR_
