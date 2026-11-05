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
#include "share/ob_io_device_helper.h"
#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include "storage/meta_store/storage_meta_service.h"
#include "share/inner_table/local_table_schema.h"
#include "storage/api/storage/runtime/ob_i_server_runtime.h"
#include "storage/meta_store/ob_server_runtime_meta.h"
#include "storage/meta_store/tablet_loader.h"
#include "storage/ob_file_system_router.h"
#include "storage/blocksstable/ob_block_manager.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ls/ob_ls.h"
#include "lib/checksum/ob_crc64.h"

namespace oceanbase { namespace storage {
using namespace common;
using namespace share;
namespace {
constexpr int64_t NORMAL_TABLET = 0;
constexpr int64_t EMPTY_TABLET = 1;

template <class T>
int encode_record(const T &object, LocalValue &value)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator("StorageMeta");
  const int64_t size = object.get_serialize_size();
  char *buf = static_cast<char *>(allocator.alloc(size));
  int64_t pos = 0;
  if (OB_ISNULL(buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(object.serialize(buf, size, pos))) {
  } else {
    value = LocalValue::blob(buf, pos);
  }
  return ret;
}

template <class T>
int decode_record(LocalRowReader &row, int column, T &object)
{
  int ret = OB_SUCCESS;
  int size = 0;
  const char *buf = static_cast<const char *>(row.get_blob(column, &size));
  int64_t pos = 0;
  if (OB_ISNULL(buf) || size <= 0) {
    ret = OB_DESERIALIZE_ERROR;
  } else if (OB_FAIL(object.deserialize(buf, size, pos))) {
  } else if (pos != size || !object.is_valid()) {
    ret = OB_DESERIALIZE_ERROR;
  }
  return ret;
}
}

StorageMetaService &StorageMetaService::get_instance()
{
  static StorageMetaService service;
  return service;
}

int StorageMetaService::init(ObIServerRuntime &runtime, LocalStore &store)
{
  int ret = OB_SUCCESS;
  if (store_ != nullptr) {
    ret = OB_INIT_TWICE;
  } else if (!store.is_inited()) {
    ret = OB_NOT_INIT;
  } else {
#ifndef _WIN32
    struct statvfs data_fs, local_fs;
    if (0 != statvfs(OB_FILE_SYSTEM_ROUTER.get_sstable_dir(), &data_fs)
        || 0 != statvfs("./store/local", &local_fs)) {
      ret = OB_IO_ERROR;
    } else {
      need_reserved_ = data_fs.f_fsid == local_fs.f_fsid;
    }
#endif
    if (OB_SUCC(ret)) {
      store_ = &store;
      runtime_ = &runtime;
    }
  }
  return ret;
}

int StorageMetaService::get_reserved_size(int64_t &size) const
{
  // Leave growth headroom for recovery metadata when both files share a device.
  size = need_reserved_ ? 128L * 1024 * 1024 : 0;
  return store_ == nullptr ? OB_NOT_INIT : OB_SUCCESS;
}

int StorageMetaService::start(bool bootstrap)
{
  int ret = OB_SUCCESS;
  if (store_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(recover(bootstrap))) {
  } else {
    ATOMIC_STORE(&is_started_, true);
  }
  return ret;
}

void StorageMetaService::destroy()
{
  stop();
  store_ = nullptr;
  runtime_ = nullptr;
  need_reserved_ = false;
}

int StorageMetaService::check_commit(int ret)
{
  if (OB_IO_ERROR == ret) {
    // A failed root sync may already have committed. Continuing could free live blocks.
    LOG_ERROR("local storage commit is uncertain; stop before releasing referenced blocks", K(ret));
    ob_abort();
  }
  return ret;
}

int StorageMetaService::write_resources(const ObServerRuntimeConfig &config)
{
  int ret = OB_SUCCESS;
  LocalValue payload;
  using namespace storage_runtime;
  if (store_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!config.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(encode_record(config, payload))) {
  } else {
    ret = check_commit(store_->write([&](LocalTransaction &tx) {
      int64_t initialized = 0;
      int result = tx.query(LocalRequest::select(storage_runtime::schema(), {Initialized}).eq(Id, int64_t(0)),
          [&](LocalRowReader &row) { initialized = row.get_int64(); return OB_SUCCESS; });
      if (OB_SUCCESS == result || OB_ENTRY_NOT_EXIST == result) {
        result = tx.execute(LocalRequest::upsert(storage_runtime::schema()).set(Id, int64_t(0))
            .set(Initialized, initialized).set(Payload, payload));
      }
      return result;
    }));
  }
  return ret;
}

int StorageMetaService::commit_bootstrap()
{
  int ret = OB_SUCCESS;
  if (store_ == nullptr || runtime_->is_hidden()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    ret = check_commit(store_->write([&](LocalTransaction &tx) {
      ObLSMeta meta;
      int result = tx.query(LocalRequest::select(storage_ls::schema(), {storage_ls::Payload})
          .eq(storage_ls::Id, int64_t(0)), [&](LocalRowReader &row) { return decode_record(row, 0, meta); });
      int64_t affected = 0;
      if (OB_SUCCESS == result) {
        result = tx.execute(LocalRequest::update(storage_runtime::schema())
            .eq(storage_runtime::Id, int64_t(0)).set(storage_runtime::Initialized, int64_t(1)), &affected);
        if (OB_SUCCESS == result && affected != 1) { result = OB_STATE_NOT_MATCH; }
      }
      return result;
    }));
  }
  return ret;
}

int StorageMetaService::update_ls_meta(const ObLSMeta &meta)
{
  int ret = OB_SUCCESS;
  LocalValue payload;
  if (store_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!meta.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(encode_record(meta, payload))) {
  } else {
    ret = check_commit(store_->execute(LocalRequest::upsert(storage_ls::schema())
        .set(storage_ls::Id, int64_t(0)).set(storage_ls::Payload, payload)));
  }
  return ret;
}

int StorageMetaService::write_tablet(const ObTabletID &id, int64_t kind, const LocalValue &payload)
{
  using namespace storage_tablet;
  return store_ == nullptr ? OB_NOT_INIT : check_commit(store_->execute(
      LocalRequest::upsert(storage_tablet::schema()).set(TabletId, static_cast<int64_t>(id.id()))
          .set(Kind, kind).set(Payload, payload)));
}

int StorageMetaService::update_tablet(const ObTabletID &id, const ObMetaDiskAddr &addr)
{
  int ret = OB_SUCCESS;
  LocalValue payload;
  if (!id.is_valid() || !addr.is_valid() || !addr.is_block()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(encode_record(addr, payload))) {
  } else if (OB_FAIL(LOCAL_DEVICE_INSTANCE.fsync_block())) {
  } else {
    ret = write_tablet(id, NORMAL_TABLET, payload);
  }
  return ret;
}

int StorageMetaService::write_empty_shell_tablet(ObTablet *tablet, ObMetaDiskAddr &addr)
{
  int ret = OB_SUCCESS;
  LocalValue payload;
  if (OB_ISNULL(tablet) || !tablet->is_empty_shell()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(encode_record(*tablet, payload))) {
  } else if (OB_FAIL(addr.set_local_addr(tablet->get_tablet_meta().tablet_id_.id(),
      payload.bytes.size(), ob_crc64(payload.bytes.data(), payload.bytes.size())))) {
  } else {
    ret = write_tablet(tablet->get_tablet_meta().tablet_id_, EMPTY_TABLET, payload);
  }
  return ret;
}

int StorageMetaService::remove_tablet(const ObTabletID &id)
{
  int ret = OB_SUCCESS;
  using namespace storage_tablet;
  if (store_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = check_commit(store_->execute(LocalRequest::erase(storage_tablet::schema())
        .eq(TabletId, static_cast<int64_t>(id.id()))));
  }
  return ret;
}

int StorageMetaService::read_empty_shell(const ObMetaDiskAddr &addr, ObArenaAllocator &allocator,
                                        char *&buf, int64_t &len)
{
  int ret = OB_SUCCESS;
  buf = nullptr;
  len = 0;
  using namespace storage_tablet;
  if (store_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (!addr.is_valid() || !addr.is_local()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = store_->query(LocalRequest::select(storage_tablet::schema(), {Kind, Payload})
        .eq(TabletId, addr.local_id()), [&](LocalRowReader &row) {
      int result = OB_SUCCESS;
      int size = 0;
      const char *data = static_cast<const char *>(row.get_blob(1, &size));
      if (row.get_int64(0) != EMPTY_TABLET || size != addr.size() || data == nullptr
          || ob_crc64(data, size) != addr.local_checksum()) {
        result = OB_SEARCH_NOT_FOUND;
      } else if (OB_ISNULL(buf = static_cast<char *>(allocator.alloc(size)))) {
        result = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(buf, data, size);
        len = size;
      }
      return result;
    });
    if (OB_ENTRY_NOT_EXIST == ret) { ret = OB_SEARCH_NOT_FOUND; }
  }
  return ret;
}

int StorageMetaService::recover(bool bootstrap)
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeMeta meta;
  int64_t initialized = 0;
  ret = store_->query(LocalRequest::select(storage_runtime::schema(),
      {storage_runtime::Initialized, storage_runtime::Payload}).eq(storage_runtime::Id, int64_t(0)),
      [&](LocalRowReader &row) { initialized = row.get_int64(0); return decode_record(row, 1, meta.runtime_config_); });
  if (bootstrap) {
    if (OB_ENTRY_NOT_EXIST == ret) {
      ret = OB_SUCCESS;
      // No prior recovery records may be paired with a newly formatted data file.
      for (const auto *schema : {&storage_ls::schema(), &storage_tablet::schema()}) {
        const int found = store_->query(LocalRequest::select(*schema, {0}).take(1),
            [](LocalRowReader &) { return OB_SUCCESS; });
        if (OB_ENTRY_NOT_EXIST != found) { ret = OB_SUCCESS == found ? OB_STATE_NOT_MATCH : found; break; }
      }
      if (OB_SUCC(ret)) { ret = OB_SERVER_BLOCK_MGR.first_mark_device(); }
    } else if (OB_SUCCESS == ret) {
      ret = OB_STATE_NOT_MATCH;
    }
  } else if (OB_FAIL(ret)) {
  } else if (initialized != 1) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    meta.is_hidden_ = false;
    ObLSMeta ls_meta;
    ObSEArray<TabletLoadItem, 16> tablets;
    if (OB_FAIL(store_->query(LocalRequest::select(storage_ls::schema(), {storage_ls::Payload})
        .eq(storage_ls::Id, int64_t(0)), [&](LocalRowReader &row) { return decode_record(row, 0, ls_meta); }))) {
    } else if (OB_FAIL(runtime_->create_runtime(meta))) {
    } else if (OB_FAIL(server_service<ObLSService>()->replay_create_ls(ls_meta))) {
    } else {
      ret = store_->query(LocalRequest::select(storage_tablet::schema(),
          {storage_tablet::TabletId, storage_tablet::Kind, storage_tablet::Payload}),
          [&](LocalRowReader &row) {
        int result = OB_SUCCESS;
        ObTabletID id(row.get_int64(0));
        ObMetaDiskAddr addr;
        if (!id.is_valid()) {
          result = OB_DESERIALIZE_ERROR;
        } else if (row.get_int64(1) == NORMAL_TABLET) {
          result = decode_record(row, 2, addr);
          if (OB_SUCCESS == result && !addr.is_block()) { result = OB_DESERIALIZE_ERROR; }
        } else if (row.get_int64(1) == EMPTY_TABLET) {
          int size = 0;
          const char *data = static_cast<const char *>(row.get_blob(2, &size));
          result = data == nullptr ? OB_DESERIALIZE_ERROR
              : addr.set_local_addr(id.id(), size, ob_crc64(data, size));
        } else {
          result = OB_DESERIALIZE_ERROR;
        }
        if (OB_SUCCESS == result) { result = tablets.push_back(TabletLoadItem(ObTabletMapKey(id), addr)); }
        return result;
      });
      if (OB_ENTRY_NOT_EXIST == ret) { ret = OB_SUCCESS; }
    }
    TabletLoader loader(tablets);
    ObLS *ls = nullptr;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(loader.load(server_service<ObStartupAccelTaskHandler>()))) {
    } else if (OB_FAIL(OB_SERVER_BLOCK_MGR.first_mark_device())) {
    } else if (OB_FAIL(server_service<ObLSService>()->get_ls(ls))) {
    } else if (OB_FAIL(ls->finish_storage_meta_replay())) {
    } else if (OB_FAIL(server_service<ObLSService>()->finish_ls_recovery())) {
    } else if (OB_FAIL(server_service<ObLSService>()->online_ls())) {
    } else {
      runtime_->set_synced();
    }
  }
  LOG_INFO("finish loading local storage metadata", K(ret), K(bootstrap));
  return ret;
}
} } // namespace oceanbase::storage
