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

#include "ob_tablet_create_delete_mds_user_data.h"
#include "share/rc/ob_module_provider.h"
#include "storage/tx_storage/ob_ls_service.h"

#define USING_LOG_PREFIX MDS

using namespace oceanbase::common;
using namespace oceanbase::transaction;

namespace oceanbase
{
namespace storage
{
ObTabletCreateDeleteMdsUserData::ObTabletCreateDeleteMdsUserData()
  : tablet_status_(ObTabletStatus::NONE),
    reserved_scn_(share::SCN::invalid_scn()),
    reserved_ls_id_(),
    data_type_(ObTabletMdsUserDataType::NONE),
    create_commit_scn_(share::SCN::invalid_scn()),
    create_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    delete_commit_scn_(share::SCN::invalid_scn()),
    delete_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    reserved_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    start_split_commit_version_(ObTransVersion::INVALID_TRANS_VERSION)
{
}

ObTabletCreateDeleteMdsUserData::ObTabletCreateDeleteMdsUserData(
    const ObTabletStatus::Status &status,
    const ObTabletMdsUserDataType &type,
    const int64_t create_commit_version)
  : tablet_status_(status),
    reserved_scn_(share::SCN::invalid_scn()),
    reserved_ls_id_(),
    data_type_(type),
    create_commit_scn_(share::SCN::invalid_scn()),
    create_commit_version_(create_commit_version),
    delete_commit_scn_(share::SCN::invalid_scn()),
    delete_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    reserved_commit_version_(ObTransVersion::INVALID_TRANS_VERSION),
    start_split_commit_version_(ObTransVersion::INVALID_TRANS_VERSION)
{
}

int ObTabletCreateDeleteMdsUserData::assign(const ObTabletCreateDeleteMdsUserData &other)
{
  int ret = OB_SUCCESS;
  tablet_status_ = other.tablet_status_;
  reserved_scn_ = other.reserved_scn_;
  reserved_ls_id_ = other.reserved_ls_id_;
  data_type_ = other.data_type_;
  create_commit_scn_ = other.create_commit_scn_;
  create_commit_version_ = other.create_commit_version_;
  delete_commit_scn_ = other.delete_commit_scn_;
  delete_commit_version_ = other.delete_commit_version_;
  reserved_commit_version_ = other.reserved_commit_version_;
  start_split_commit_version_ = other.start_split_commit_version_;
  return ret;
}

void ObTabletCreateDeleteMdsUserData::reset()
{
  tablet_status_ = ObTabletStatus::NONE;
  reserved_scn_.set_invalid();
  reserved_ls_id_.reset();
  data_type_ = ObTabletMdsUserDataType::NONE;
  create_commit_scn_.set_invalid();
  create_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
  delete_commit_scn_.set_invalid();
  delete_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
  reserved_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
  start_split_commit_version_ = ObTransVersion::INVALID_TRANS_VERSION;
}

void ObTabletCreateDeleteMdsUserData::on_init()
{
  reset();
  tablet_status_ = ObTabletStatus::NONE;
  data_type_ = ObTabletMdsUserDataType::NONE;
}

void ObTabletCreateDeleteMdsUserData::on_redo(const share::SCN &redo_scn)
{
  int ret = OB_SUCCESS;
  switch (data_type_) {
  case ObTabletMdsUserDataType::NONE :
  case ObTabletMdsUserDataType::CREATE_TABLET :
  case ObTabletMdsUserDataType::REMOVE_TABLET :
  case ObTabletMdsUserDataType::RESERVED_7:
  case ObTabletMdsUserDataType::RESERVED_4 :
  case ObTabletMdsUserDataType::RESERVED_5 :
  case ObTabletMdsUserDataType::START_SPLIT_SRC :
  case ObTabletMdsUserDataType::START_SPLIT_DST :
  case ObTabletMdsUserDataType::FINISH_SPLIT_SRC :
  case ObTabletMdsUserDataType::FINISH_SPLIT_DST : {
    break;
  }
  case ObTabletMdsUserDataType::RESERVED_3 :
  case ObTabletMdsUserDataType::RESERVED_6 : {
    reserved_scn_on_redo_(redo_scn);
    break;
  }
  default: {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid cur status for fail", K(ret), KPC(this));
  }
  }
}

void ObTabletCreateDeleteMdsUserData::reserved_scn_on_redo_(const share::SCN &redo_scn)
{
  reserved_scn_ = redo_scn;
  LOG_INFO("reserved tablet status redo", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::on_commit(const share::SCN &commit_version, const share::SCN &commit_scn)
{
  int ret = OB_SUCCESS;
  switch (data_type_) {
  case ObTabletMdsUserDataType::NONE :
  case ObTabletMdsUserDataType::RESERVED_7:
  case ObTabletMdsUserDataType::RESERVED_6 :
  case ObTabletMdsUserDataType::FINISH_SPLIT_DST : {
    break;
  }
  case ObTabletMdsUserDataType::CREATE_TABLET : {
    create_tablet_on_commit_(commit_version, commit_scn);
    break;
  }
  case ObTabletMdsUserDataType::RESERVED_4 : {
    reserved_start_on_commit_(commit_version);
    break;
  }
  case ObTabletMdsUserDataType::REMOVE_TABLET : {
    delete_tablet_on_commit_(commit_version, commit_scn);
    break;
  }
  case ObTabletMdsUserDataType::RESERVED_5 : {
    reserved_finish_on_commit_(commit_version, commit_scn);
    break;
  }
  case ObTabletMdsUserDataType::RESERVED_3 : {
    reserved_start_on_commit_(commit_version);
    break;
  }
  case ObTabletMdsUserDataType::START_SPLIT_SRC : {
    start_split_src_on_commit_(commit_version);
    break;
  }
  case ObTabletMdsUserDataType::START_SPLIT_DST : {
    start_split_dst_on_commit_(commit_version);
    break;
  }
  case ObTabletMdsUserDataType::FINISH_SPLIT_SRC : {
    finish_split_src_on_commit_(commit_version, commit_scn);
    break;
  }
  default: {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid cur status for fail", K(ret), KPC(this));
  }
  }
}

void ObTabletCreateDeleteMdsUserData::create_tablet_on_commit_(
    const share::SCN &commit_version,
    const share::SCN &commit_scn)
{
  create_commit_scn_ = commit_scn;
  create_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("create tablet commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::delete_tablet_on_commit_(
    const share::SCN &commit_version,
    const share::SCN &commit_scn)
{
  delete_commit_scn_ = commit_scn;
  delete_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("delete tablet commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::reserved_start_on_commit_(
    const share::SCN &commit_version)
{
  reserved_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("reserved tablet status start commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::reserved_finish_on_commit_(
    const share::SCN &commit_version,
    const share::SCN &commit_scn)
{
  delete_commit_scn_ = commit_scn;
  delete_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("reserved tablet status finish commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::start_split_src_on_commit_(
    const share::SCN &commit_version)
{
  start_split_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("start split src on commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::start_split_dst_on_commit_(
    const share::SCN &commit_version)
{
  start_split_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("start split dst on commit", KPC(this));
}

void ObTabletCreateDeleteMdsUserData::finish_split_src_on_commit_(
    const share::SCN &commit_version,
    const share::SCN &commit_scn)
{
  delete_commit_scn_ = commit_scn;
  delete_commit_version_ = commit_version.get_val_for_tx();
  LOG_INFO("split src delete tablet commit", KPC(this));
}

int ObTabletCreateDeleteMdsUserData::set_tablet_gc_trigger(
    const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObLSService *ls_service = share::g_mp->ls_service();
  if (OB_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::MDS_TABLE_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret), K(ls_id), K(ls_handle));
  } else {
    ls->get_tablet_gc_handler()->set_tablet_gc_trigger();
  }
  return ret;
}

int ObTabletCreateDeleteMdsUserData::set_tablet_empty_shell_trigger(
    const share::ObLSID &ls_id)
{
  int ret = OB_SUCCESS;
  ObLSHandle ls_handle;
  ObLS *ls = nullptr;
  ObLSService *ls_service = share::g_mp->ls_service();
  if (OB_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::MDS_TABLE_MOD))) {
    LOG_WARN("failed to get ls", K(ret), K(ls_id));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", K(ret), K(ls_id), K(ls_handle));
  } else {
    ls->get_tablet_empty_shell_handler()->set_empty_shell_trigger(true);
    LOG_INFO("set tablet empty shell trigger", K(ret), K(ls_id), "handler", ls->get_tablet_empty_shell_handler());
  }
  return ret;
}

OB_SERIALIZE_MEMBER(
    ObTabletCreateDeleteMdsUserData,
    tablet_status_,
    reserved_scn_,
    reserved_ls_id_,
    data_type_,
    create_commit_scn_,
    create_commit_version_,
    delete_commit_scn_,
    delete_commit_version_,
    reserved_commit_version_,
    start_split_commit_version_
)

} // namespace storage
} // namespace oceanbase
