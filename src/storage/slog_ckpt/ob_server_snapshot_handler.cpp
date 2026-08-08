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

#include "ob_server_snapshot_handler.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "src/storage/ls/ob_ls.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"


namespace oceanbase
{
using namespace common;
using namespace blocksstable;
namespace storage
{
int ObServerSnapshotHandler::create_server_snapshot(const ObServerSnapshotID &snapshot_id)
{
  int ret = OB_SUCCESS;
  ObIServerRuntime *runtime = ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>();
  const ObServerRuntimeSuperBlock last_super_block = runtime->get_super_block();
  ObServerSnapshotMeta snapshot;
  snapshot.snapshot_id_ = snapshot_id;

  if (OB_UNLIKELY(!snapshot_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(snapshot_id));
  } else if (OB_FAIL(last_super_block.check_new_snapshot(snapshot_id))) {
  } else if (OB_UNLIKELY(runtime->is_hidden())) {
    ret = OB_NOT_SUPPORTED;
    LOG_INFO("shouldn't create snapshot for hidden runtime", K(ret));
  } else if (OB_UNLIKELY(!last_super_block.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get runtime super block", K(ret), K(last_super_block));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->add_snapshot(snapshot))) {
  }

  FLOG_INFO("finish creating runtime snapshot", K(ret), K(last_super_block));
  return ret;
}

int ObServerSnapshotHandler::inc_all_linked_block_ref(
    ObLocalStorageCheckpointWriter &local_storage_writer,
    bool &inc_ls_blocks_ref_succ,
    bool &inc_tablet_blocks_ref_succ)
{
  int ret = OB_SUCCESS;
  ObIArray<MacroBlockId> *meta_block_list = nullptr;
  if (OB_FAIL(local_storage_writer.get_ls_block_list(meta_block_list))) {
  } else if (OB_FAIL(inc_linked_block_ref(*meta_block_list, inc_ls_blocks_ref_succ))) {
  } else if (OB_FAIL(local_storage_writer.get_tablet_block_list(meta_block_list))) {
  } else if (OB_FAIL(inc_linked_block_ref(*meta_block_list, inc_tablet_blocks_ref_succ))) {
  }
  return ret;
}

void ObServerSnapshotHandler::rollback_ref_cnt(
    const bool inc_ls_blocks_ref_succ,
    const bool inc_tablet_blocks_ref_succ,
    ObLocalStorageCheckpointWriter &local_storage_writer)
{
  int ret = OB_SUCCESS;
  ObIArray<MacroBlockId> *meta_block_list = nullptr;
  // ignore all ret, because we need to rollback the ref cnt as much as possible
  if (OB_FAIL(local_storage_writer.rollback())) {
  }
  if (inc_ls_blocks_ref_succ) {
    if (OB_FAIL(local_storage_writer.get_ls_block_list(meta_block_list))) {
    } else {
      dec_meta_block_ref(*meta_block_list);
    }
  }
  if (inc_tablet_blocks_ref_succ) {
    if (OB_FAIL(local_storage_writer.get_tablet_block_list(meta_block_list))) {
    } else {
      dec_meta_block_ref(*meta_block_list);
    }
  }
}

int ObServerSnapshotHandler::get_ls_meta_entry(
    const ObServerSnapshotID &snapshot_id,
    blocksstable::MacroBlockId &ls_meta_entry)
{
  int ret = OB_SUCCESS;
  ObServerSnapshotMeta snapshot;
  ObIServerRuntime *runtime = ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>();
  const ObServerRuntimeSuperBlock super_block = runtime->get_super_block();
  if (OB_UNLIKELY(!snapshot_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(snapshot_id));
  } else if (OB_UNLIKELY(runtime->is_hidden())) {
    ret = OB_NOT_SUPPORTED;
    LOG_INFO("shouldn't get snapshot from hidden runtime", K(ret));
  } else if (OB_UNLIKELY(!super_block.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get runtime super block", K(ret), K(super_block));
  } else if (OB_FAIL(super_block.get_snapshot(snapshot_id, snapshot))) {
  } else {
    ls_meta_entry = snapshot.ls_meta_entry_;
  }
  return ret;
}

int ObServerSnapshotHandler::inc_linked_block_ref(
    const ObIArray<blocksstable::MacroBlockId> &meta_block_list,
    bool &inc_success)
{
  int ret = OB_SUCCESS;
  inc_success = false;
  int64_t meta_block_num = 0;

  for (int64_t i = 0; OB_SUCC(ret) && i < meta_block_list.count(); i++) {
    if (OB_FAIL(OB_STORAGE_OBJECT_MGR.inc_ref(meta_block_list.at(i)))) {
    } else {
      meta_block_num++;
    }
  }
  if (OB_FAIL(ret)) {
    int tmp_ret = OB_SUCCESS;
    for (int64_t i = 0; i < meta_block_num; i++) {
      if (OB_TMP_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(meta_block_list.at(i)))) {
      }
    }
  } else {
    inc_success = true;
  }
  return ret;
}

void ObServerSnapshotHandler::dec_meta_block_ref(const ObIArray<blocksstable::MacroBlockId> &meta_block_list)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; i < meta_block_list.count(); i++) {
    if (OB_FAIL(OB_STORAGE_OBJECT_MGR.dec_ref(meta_block_list.at(i)))) {
    }
  }
}

int ObServerSnapshotHandler::delete_server_snapshot(const ObServerSnapshotID &snapshot_id)
{
  int ret = OB_SUCCESS;
  ObIServerRuntime *runtime = ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>();
  const ObServerRuntimeSuperBlock last_super_block = runtime->get_super_block();
  ObLocalStorageCheckpointReader ls_snapshot_reader;
  ObServerSnapshotMeta snapshot;
  ObSArray<MacroBlockId> ls_meta_block_list(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("DelSnap"));
  ObSArray<ObMetaDiskAddr> deleted_tablet_addrs(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("DelSnap"));
  ObSArray<MacroBlockId> tablet_meta_block_list(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("DelSnap"));
  ObLocalStorageCheckpointReader::ObStorageMetaOp del_ls_snapshot_op = std::bind(
      &ObServerSnapshotHandler::delete_ls_snapshot,
      std::placeholders::_1,
      std::placeholders::_2,
      std::placeholders::_3,
      std::ref(deleted_tablet_addrs),
      std::ref(tablet_meta_block_list));

  if (OB_UNLIKELY(!snapshot_id.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(snapshot_id));
  } else if (OB_UNLIKELY(runtime->is_hidden())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("can't delete snapshot for hidden runtime", K(ret));
  } else if (OB_UNLIKELY(!last_super_block.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("super block is invalid", K(ret), K(last_super_block));
  } else if (OB_FAIL(last_super_block.get_snapshot(snapshot_id, snapshot))) {
  } else if (OB_FAIL(ls_snapshot_reader.read_single_meta_item(
      snapshot.ls_meta_entry_, del_ls_snapshot_op, ls_meta_block_list))) {
  } else if (OB_FAIL((::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->delete_snapshot(snapshot_id)))) {
  } else {
    dec_meta_block_ref(ls_meta_block_list);
    dec_meta_block_ref(tablet_meta_block_list);
    if (OB_FAIL(inner_delete_tablet_by_addrs(deleted_tablet_addrs))) {
    }
  }

  FLOG_INFO("finish deleting runtime snapshot", K(ret), K(last_super_block));
  return ret;
}

int ObServerSnapshotHandler::inner_delete_ls_snapshot(
    const blocksstable::MacroBlockId& tablet_meta_entry,
    ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs,
    ObIArray<MacroBlockId> &tablet_meta_block_list)
{
  int ret = OB_SUCCESS;
  ObLocalStorageCheckpointReader tablet_snapshot_reader;
  ObSArray<MacroBlockId> meta_block_list(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("SnapTablet"));
  ObLocalStorageCheckpointReader::ObStorageMetaOp del_tablet_snapshot_op = std::bind(
      &ObServerSnapshotHandler::delete_tablet_snapshot,
      std::placeholders::_1,
      std::placeholders::_2,
      std::placeholders::_3,
      std::ref(deleted_tablet_addrs));

  if (OB_FAIL(tablet_snapshot_reader.iter_read_meta_item(
      tablet_meta_entry, del_tablet_snapshot_op, meta_block_list))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < meta_block_list.count(); i++) {
      if (OB_FAIL(tablet_meta_block_list.push_back(meta_block_list.at(i)))) {
      }
    }
  }
  return ret;
}

int ObServerSnapshotHandler::delete_ls_snapshot(
    const ObMetaDiskAddr &addr,
    const char *buf,
    const int64_t buf_len,
    ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs,
    ObIArray<MacroBlockId> &tablet_meta_block_list)
{
  UNUSED(addr);
  int ret = OB_SUCCESS;
  ObLSCkptMember ls_ckpt_member;
  int64_t pos = 0;

  if (OB_FAIL(ls_ckpt_member.deserialize(buf, buf_len, pos))) {
  } else if (OB_FAIL(inner_delete_ls_snapshot(ls_ckpt_member.tablet_meta_entry_,
                                              deleted_tablet_addrs,
                                              tablet_meta_block_list))) {
  }

  return ret;
}

int ObServerSnapshotHandler::inner_delete_tablet_by_addrs(
    const ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs)
{
  int ret = OB_SUCCESS;

  ObArenaAllocator arena_allocator("DelSnapTablet", OB_MALLOC_NORMAL_BLOCK_SIZE);
  ObTablet tablet;
  for (int64_t i = 0; i < deleted_tablet_addrs.count(); i++) {
    tablet.reset();
    arena_allocator.reuse();
    int64_t buf_len = 0;
    char *buf = nullptr;
    int64_t pos = 0;
    do {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->read_from_disk(
          deleted_tablet_addrs.at(i),
          arena_allocator,
          buf,
          buf_len))) {
      }
    } while (ObLocalStorageCheckpointWriter::ignore_ret(ret));
    if (OB_SUCC(ret)) {
      tablet.set_tablet_addr(deleted_tablet_addrs.at(i));
      if (OB_FAIL(tablet.release_ref_cnt(arena_allocator, buf, buf_len, pos))) {
      }
    }
  }
  return ret;
}

int ObServerSnapshotHandler::delete_tablet_snapshot(
    const ObMetaDiskAddr &addr,
    const char *buf,
    const int64_t buf_len,
    ObIArray<ObMetaDiskAddr> &deleted_tablet_addrs)
{
  UNUSED(addr);
  int ret = OB_SUCCESS;
  ObUpdateTabletLog slog;
  int64_t pos = 0;
  if (OB_FAIL(slog.deserialize(buf, buf_len, pos))) {
  } else if (OB_UNLIKELY(!slog.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("slog is invalid", K(ret), K(slog));
  } else if (OB_FAIL(deleted_tablet_addrs.push_back(slog.disk_addr_))) {
  }
  return ret;
}

int ObServerSnapshotHandler::get_all_server_snapshots(ObIArray<ObServerSnapshotID> &snapshot_ids)
{
  int ret = OB_SUCCESS;
  ObIServerRuntime *runtime = ::oceanbase::share::server_service<::oceanbase::storage::ObIServerRuntime>();
  const ObServerRuntimeSuperBlock super_block = runtime->get_super_block();

  if (OB_UNLIKELY(runtime->is_hidden())) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("can't get snapshot from hidden runtime", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < super_block.snapshot_cnt_; i++) {
      const ObServerSnapshotMeta &snapshot = super_block.snapshots_[i];
      if (OB_UNLIKELY(!snapshot.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("snapshot meta is invalid", K(ret), K(snapshot));
      } else if (OB_FAIL(snapshot_ids.push_back(snapshot.snapshot_id_))) {
      }
    }
  }
  return ret;
}

int ObServerSnapshotHandler::create_all_tablet(ObStartupAccelTaskHandler* startup_accel_handler,
                                               const blocksstable::MacroBlockId &tablet_meta_entry)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!tablet_meta_entry.is_valid() || IS_EMPTY_BLOCK_LIST(tablet_meta_entry))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(tablet_meta_entry));
  }

  if (OB_SUCC(ret)) {
    ObLocalStorageCheckpointReader tablet_snapshot_reader;
    ObSArray<MacroBlockId> meta_block_list(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator("SnapCreate"));
    ObSArray<ObUpdateTabletLog> slog_arr;
    slog_arr.set_attr(ObMemAttr("SnapRecovery"));

    ObLocalStorageCheckpointReader::ObStorageMetaOp write_slog_op = std::bind(
        &ObServerSnapshotHandler::batch_write_slog,
        std::placeholders::_1,
        std::placeholders::_2,
        std::placeholders::_3,
        std::ref(slog_arr));

    if (OB_FAIL(tablet_snapshot_reader.iter_read_meta_item(tablet_meta_entry, write_slog_op, meta_block_list))) {
    } else if (0 != slog_arr.count() && OB_FAIL(do_write_slog(slog_arr))) {
      LOG_WARN("fail to write and report slogs", K(ret), K(slog_arr));
    } else {
      FLOG_INFO("write all tablet slog done");
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL((::oceanbase::share::server_service<::oceanbase::storage::ObLocalStorageMetaService>()->clone_ls(startup_accel_handler, tablet_meta_entry)))) {
    }
  }
  return ret;
}

int ObServerSnapshotHandler::batch_write_slog(
    const ObMetaDiskAddr &addr,
    const char *buf,
    const int64_t buf_len,
    ObIArray<ObUpdateTabletLog> &slog_arr)
{
  UNUSED(addr);
  int ret = OB_SUCCESS;
  ObUpdateTabletLog slog;
  ObStorageLogParam log_param;
  int64_t pos = 0;

  if (MAX_SLOG_BATCH_NUM <= slog_arr.count()) {
    if (OB_FAIL(do_write_slog(slog_arr))) {
    } else {
      slog_arr.reuse();
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(slog.deserialize(buf, buf_len, pos))) {
  } else if (OB_UNLIKELY(!slog.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("slog is invalid", K(ret), K(slog));
  } else if (OB_FAIL(slog_arr.push_back(slog))) {
  }
  return ret;
}

int ObServerSnapshotHandler::do_write_slog(ObIArray<ObUpdateTabletLog> &slog_arr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(LOCAL_STORAGE_META_PERSISTER.batch_update_tablet(slog_arr))) {
  }
  return ret;
}


}
}
