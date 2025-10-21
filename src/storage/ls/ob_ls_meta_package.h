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

#ifndef OCEANBASE_STORAGE_OB_LS_META_PACKAGE_
#define OCEANBASE_STORAGE_OB_LS_META_PACKAGE_
#include "storage/ls/ob_ls_meta.h"               // ObLSMeta
#include "logservice/palf/palf_base_info.h"      // PalfBaseInfo

namespace oceanbase
{
namespace storage
{

// this is a package of meta.
// it is a combination of ls meta and all its member's.
// we can rebuild the ls with this package.
class ObLSMetaPackage final
{
  OB_UNIS_VERSION_V(1);
public:
  ObLSMetaPackage();
  ObLSMetaPackage(const ObLSMetaPackage &ls_meta);
  ~ObLSMetaPackage() { reset(); }
  ObLSMetaPackage &operator=(const ObLSMetaPackage &other);
  void reset();
  bool is_valid() const;
  void update_clog_checkpoint_in_ls_meta(const share::SCN& clog_checkpoint_scn,
                                         const palf::LSN& clog_base_lsn);

  TO_STRING_KV(K_(ls_meta), K_(palf_meta));
public:
  ObLSMeta ls_meta_;                // the meta of ls
  palf::PalfBaseInfo palf_meta_;    // the meta of palf
  share::SCN tx_data_recycle_scn_;
};

} // storage
} // oceanbase
#endif
