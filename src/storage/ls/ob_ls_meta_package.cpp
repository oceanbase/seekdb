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

#include "storage/ls/ob_ls_meta_package.h"

namespace oceanbase
{
namespace storage
{
OB_SERIALIZE_MEMBER(ObLSMetaPackage,
                    ls_meta_,
                    palf_meta_,
                    tx_data_recycle_scn_);

ObLSMetaPackage::ObLSMetaPackage()
  : ls_meta_(),
    palf_meta_(),
    tx_data_recycle_scn_()
{
}

ObLSMetaPackage::ObLSMetaPackage(const ObLSMetaPackage &other)
    : ls_meta_(other.ls_meta_), palf_meta_(other.palf_meta_), tx_data_recycle_scn_(other.tx_data_recycle_scn_)
{
}

ObLSMetaPackage &ObLSMetaPackage::operator=(const ObLSMetaPackage &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    ls_meta_ = other.ls_meta_;
    palf_meta_ = other.palf_meta_;
    tx_data_recycle_scn_ = other.tx_data_recycle_scn_;
  }
  return *this;
}

void ObLSMetaPackage::reset()
{
  ls_meta_.reset();
  palf_meta_.reset();
  tx_data_recycle_scn_.reset();
}

bool ObLSMetaPackage::is_valid() const
{
  return (ls_meta_.is_valid() &&
          palf_meta_.is_valid());
}

void ObLSMetaPackage::update_clog_checkpoint_in_ls_meta(const share::SCN& clog_checkpoint_scn,
                                                        const palf::LSN& clog_base_lsn)
{
  ls_meta_.update_clog_checkpoint_in_ls_meta_package_(clog_checkpoint_scn, clog_base_lsn);
}

}
}
