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

#define USING_LOG_PREFIX RS
#include "rootserver/fork_table/ob_fork_table_info_builder.h"
#include "lib/container/ob_array.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace rootserver
{

ObForkTableInfoBuilder::~ObForkTableInfoBuilder()
{
  fork_table_infos_.destroy();
}

int ObForkTableInfoBuilder::init_with_fork_table_info(
    const share::ObForkTableInfo &main_fork_table_info,
    const common::ObIArray<uint64_t> &dest_table_ids,
    share::schema::ObSchemaGetterGuard &schema_guard)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObForkTableInfoBuilder init twice", KR(ret));
  } else if (OB_UNLIKELY(!main_fork_table_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid main fork table info", KR(ret), K(main_fork_table_info));
  } else if (OB_FAIL(fork_table_infos_.create(dest_table_ids.count(), "ForkTableInfo"))) {
    LOG_WARN("fail to create fork table infos map", KR(ret), "count", dest_table_ids.count());
  } else if (OB_FAIL(generate_fork_table_infos_(
              main_fork_table_info,
              dest_table_ids,
              schema_guard,
              fork_table_infos_))) {
    LOG_WARN("fail to generate fork table infos", KR(ret), K(main_fork_table_info), K(dest_table_ids));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObForkTableInfoBuilder::build_fork_tablet_infos(
    const common::ObIArray<const share::schema::ObTableSchema*> &schemas,
    const int64_t part_idx,
    const int64_t subpart_idx,
    share::schema::ObSchemaGetterGuard &schema_guard,
    common::ObIArray<share::ObForkTabletInfo> &fork_tablet_infos)
{
  int ret = OB_SUCCESS;
  if (!has_fork_table()) {
    // no-op
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < schemas.count(); ++i) {
      const share::schema::ObTableSchema *table_schema = schemas.at(i);
      share::ObForkTableInfo fork_table_info;
      share::ObForkTabletInfo fork_tablet_info;
      if (OB_ISNULL(table_schema)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("table schema is null", KR(ret), K(i), K(schemas.count()));
      } else {
        const uint64_t table_id = table_schema->get_table_id();
        if (OB_FAIL(fork_table_infos_.get_refactored(table_id, fork_table_info))) {
          if (OB_HASH_NOT_EXIST == ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("fork table info not found for table", KR(ret), K(table_id));
          } else {
            LOG_WARN("fail to get fork table info", KR(ret), K(table_id));
          }
        } else if (OB_FAIL(generate_fork_tablet_info_(part_idx,
                                                      subpart_idx,
                                                      fork_table_info,
                                                      schema_guard,
                                                      fork_tablet_info))) {
          LOG_WARN("fail to generate fork tablet info", KR(ret), K(table_id),
                   K(part_idx), K(subpart_idx), K(fork_table_info));
        } else if (OB_FAIL(fork_tablet_infos.push_back(fork_tablet_info))) {
          LOG_WARN("fail to push back fork tablet info", KR(ret), K(fork_tablet_info));
        } else {
          LOG_DEBUG("generate fork tablet info from fork table info",
              K(part_idx), K(subpart_idx), K(fork_table_info), K(fork_tablet_info), K(table_id));
        }
      }
    }
  }
  return ret;
}

int ObForkTableInfoBuilder::generate_fork_table_infos_(
    const share::ObForkTableInfo &main_fork_table_info,
    const common::ObIArray<uint64_t> &dest_table_ids,
    share::schema::ObSchemaGetterGuard &schema_guard,
    hash::ObHashMap<uint64_t, share::ObForkTableInfo> &fork_table_infos)
{
  int ret = OB_SUCCESS;
  const share::schema::ObTableSchema *src_main_table_schema = nullptr;
  int64_t src_main_table_id = OB_INVALID_ID;
  int64_t fork_snapshot_version = 0;
  common::ObArray<uint64_t> src_table_ids;

  if (OB_UNLIKELY(!main_fork_table_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("main fork table info is invalid", KR(ret), K(main_fork_table_info));
  } else if (FALSE_IT(src_main_table_id = main_fork_table_info.get_fork_src_table_id())) {
  } else if (FALSE_IT(fork_snapshot_version = main_fork_table_info.get_fork_snapshot_version())) {
  } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_,
                                                   src_main_table_id,
                                                   src_main_table_schema))) {
    LOG_WARN("fail to main table schema", KR(ret), K(tenant_id_), K(src_main_table_id));
  } else if (OB_ISNULL(src_main_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("src main table not exist", KR(ret), K(src_main_table_id));
  } else if (OB_FAIL(share::ObForkTableUtil::collect_table_ids_from_table(*src_main_table_schema, src_table_ids))) {
    LOG_WARN("fail to collect src table ids", KR(ret), KPC(src_main_table_schema));
  } else {
    if (OB_UNLIKELY(src_table_ids.count() != dest_table_ids.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("src table ids count not match dest table ids count", KR(ret),
               "src_count", src_table_ids.count(), "dest_count", dest_table_ids.count());
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < src_table_ids.count(); ++i) {
        const uint64_t src_table_id = src_table_ids.at(i);
        const uint64_t dest_table_id = dest_table_ids.at(i);
        share::ObForkTableInfo fork_table_info;
        fork_table_info.set_fork_src_table_id(src_table_id);
        fork_table_info.set_fork_snapshot_version(fork_snapshot_version);
        if (OB_FAIL(fork_table_infos.set_refactored(dest_table_id, fork_table_info))) {
          LOG_WARN("fail to push back fork table info", KR(ret), K(fork_table_info));
        } else {
          LOG_DEBUG("generate fork table info", K(src_table_id), K(dest_table_id), K(fork_table_info));
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    LOG_INFO("generate fork table infos finished",
        "dest_cnt", dest_table_ids.count(), "src_cnt", src_table_ids.count(),
        K(fork_snapshot_version));
  }

  return ret;
}

int ObForkTableInfoBuilder::generate_fork_tablet_info_(
  const int64_t part_idx,
  const int64_t subpart_idx,
  const share::ObForkTableInfo &fork_table_info,
  share::schema::ObSchemaGetterGuard &schema_guard,
  share::ObForkTabletInfo &fork_tablet_info)
{
  int ret = OB_SUCCESS;
  const share::schema::ObTableSchema *src_table_schema = nullptr;
  ObTabletID src_tablet_id;
  src_tablet_id.reset();
  if (OB_UNLIKELY(!fork_table_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid fork table info", KR(ret), K(fork_table_info));
  } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id_, fork_table_info.get_fork_src_table_id(), src_table_schema))) {
    LOG_WARN("fail to get source table schema", KR(ret), K(tenant_id_), K(fork_table_info.get_fork_src_table_id()));
  } else if (OB_ISNULL(src_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("source table not exist", KR(ret), K(fork_table_info.get_fork_src_table_id()));
  } else {
    if (share::schema::PARTITION_LEVEL_ZERO == src_table_schema->get_part_level()) {
      src_tablet_id = src_table_schema->get_tablet_id();
    } else {
      share::schema::ObBasePartition *src_part = nullptr;
      if (OB_FAIL(src_table_schema->get_part_by_idx(part_idx, subpart_idx, src_part))) {
        LOG_WARN("fail to get source part", KR(ret), KPC(src_table_schema), K(part_idx), K(subpart_idx));
      } else if (OB_ISNULL(src_part)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("source part is null", KR(ret), KPC(src_table_schema), K(part_idx), K(subpart_idx));
      } else {
        src_tablet_id = src_part->get_tablet_id();
      }
    }
  }

  if (OB_SUCC(ret)) {
    fork_tablet_info.set_fork_src_tablet_id(src_tablet_id);
    fork_tablet_info.set_fork_snapshot_version(fork_table_info.get_fork_snapshot_version());
    fork_tablet_info.clear_complete();
  }

  return ret;
}

} // rootserver
} // oceanbase
