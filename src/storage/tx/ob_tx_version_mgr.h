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

#ifndef OCEANBASE_TRANSACTION_OB_TX_VERSION_MGR_
#define OCEANBASE_TRANSACTION_OB_TX_VERSION_MGR_

#include <stdint.h>
#include "lib/atomic/ob_atomic.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace transaction
{

class ObTxVersionMgr
{
public:
  ObTxVersionMgr()
  {
    max_commit_ts_.set_base();
    max_elr_commit_ts_.set_base();
    max_read_ts_.set_base();
  }
  ~ObTxVersionMgr() {}
public:
  void update_max_commit_ts(const share::SCN &ts, const bool elr)
  {
    if (!elr) {
      max_commit_ts_.inc_update(ts);
    } else {
      max_elr_commit_ts_.inc_update(ts);
    }
#ifdef ENABLE_DEBUG_LOG
    TRANS_LOG(TRACE, "update max commit ts", K(ts), K(elr));
#endif
  }
  void update_max_read_ts(const share::SCN &ts)
  {
    max_read_ts_.inc_update(ts);
#ifdef ENABLE_DEBUG_LOG
    TRANS_LOG(TRACE, "update max read ts", K(ts));
#endif
  }
  share::SCN get_max_commit_ts(const bool elr) const
  {
    share::SCN max_commit_ts = max_commit_ts_.atomic_get();
    if (elr) {
      const share::SCN max_elr_commit_ts = max_elr_commit_ts_.atomic_get();
      max_commit_ts = share::SCN::max(max_commit_ts, max_elr_commit_ts);
    }
#ifdef ENABLE_DEBUG_LOG
    TRANS_LOG(TRACE, "get max commit ts", K(max_commit_ts), K(elr));
#endif
    return max_commit_ts;
  }
  share::SCN get_max_read_ts() const
  {
    const share::SCN max_read_ts = share::SCN::scn_inc(max_read_ts_);
#ifdef ENABLE_DEBUG_LOG
    TRANS_LOG(TRACE, "get max read ts", K(max_read_ts));
#endif
    return max_read_ts;
  }
private:
  share::SCN max_commit_ts_ CACHE_ALIGNED;
  share::SCN max_elr_commit_ts_ CACHE_ALIGNED;
  share::SCN max_read_ts_ CACHE_ALIGNED;
};

}
}//end of namespace oceanbase

#endif //OCEANBASE_TRANSACTION_OB_TX_VERSION_MGR_
