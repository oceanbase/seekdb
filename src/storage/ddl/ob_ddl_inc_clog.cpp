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

 #define USING_LOG_PREFIX STORAGE

 #include "storage/ddl/ob_ddl_inc_clog.h"

 namespace oceanbase
 {
 namespace storage
 {
 using namespace common;

 ObDDLIncLogBasic::ObDDLIncLogBasic()
   : tablet_id_(), lob_meta_tablet_id_()
 {
 }

 int ObDDLIncLogBasic::init(const ObTabletID &tablet_id, const ObTabletID &lob_meta_tablet_id)
 {
   int ret = OB_SUCCESS;
   if (OB_UNLIKELY(!tablet_id.is_valid())) {
     ret = OB_INVALID_ARGUMENT;
     LOG_WARN("invalid argument", KR(ret), K(tablet_id));
   } else {
     tablet_id_ = tablet_id;
     lob_meta_tablet_id_ = lob_meta_tablet_id;
   }

   return ret;
 }

 uint64_t ObDDLIncLogBasic::hash() const
 {
   uint64_t hash_val = 0;
   hash_val = common::murmurhash(&tablet_id_, sizeof(tablet_id_), hash_val);
   hash_val = common::murmurhash(&lob_meta_tablet_id_, sizeof(lob_meta_tablet_id_), hash_val);
   return hash_val;
 }

 int ObDDLIncLogBasic::hash(uint64_t &hash_val) const
 {
   hash_val = hash();
   return OB_SUCCESS;
 }

 OB_SERIALIZE_MEMBER(ObDDLIncLogBasic, tablet_id_, lob_meta_tablet_id_);

 ObDDLIncStartLog::ObDDLIncStartLog()
   : log_basic_()
 {
 }

 int ObDDLIncStartLog::init(const ObDDLIncLogBasic &log_basic)
 {
   int ret = OB_SUCCESS;
   if (OB_UNLIKELY(!log_basic.is_valid())) {
     ret = OB_INVALID_ARGUMENT;
     LOG_WARN("invalid argument", KR(ret), K(log_basic));
   } else {
     log_basic_ = log_basic;
   }

   return ret;
 }

 OB_SERIALIZE_MEMBER(ObDDLIncStartLog, log_basic_);

 ObDDLIncCommitLog::ObDDLIncCommitLog()
   : log_basic_()
 {
 }

 int ObDDLIncCommitLog::init(const ObDDLIncLogBasic &log_basic)
 {
   int ret = OB_SUCCESS;
   if (OB_UNLIKELY(!log_basic.is_valid())) {
     ret = OB_INVALID_ARGUMENT;
     LOG_WARN("invalid argument", KR(ret), K(log_basic));
   } else {
     log_basic_ = log_basic;
   }

   return ret;
 }

 OB_SERIALIZE_MEMBER(ObDDLIncCommitLog, log_basic_);

 } // namespace storage
 } // namespace oceanbase
