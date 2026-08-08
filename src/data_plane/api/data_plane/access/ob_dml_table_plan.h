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

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_DML_TABLE_PLAN_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_DML_TABLE_PLAN_H_

#include <stdint.h>
#include "lib/string/ob_string.h"
#include "lib/utility/ob_print_utils.h"
#include "share/schema/ob_col_desc.h"
#include "share/schema/ob_schema_struct.h"
#include "data_plane/fts/ob_doc_id.h"

namespace oceanbase
{
namespace common
{
class ObIAllocator;
template <typename T> class ObIArray;
}
namespace share
{
namespace schema
{
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace data_plane
{

class ObDmlTablePlanAccess;
class ObIDmlTablePlanState
{
public:
  virtual ~ObIDmlTablePlanState() {}
  virtual void destroy() = 0;
  virtual void reset() = 0;
  virtual bool is_valid() const = 0;
  virtual int build(const share::schema::ObTableSchema *table_schema,
                    int64_t tenant_schema_version,
                    const common::ObIArray<uint64_t> &column_ids) = 0;
  virtual int set_data_table_rowkey_tags(share::schema::ObSchemaGetterGuard *guard,
                                         const share::schema::ObTableSchema *index_schema) = 0;
  virtual int configure_multivalue_index(int64_t data_table_rowkey_column_num) = 0;
  virtual void set_has_async_index(bool has_async_index) = 0;
  virtual const common::ObIArray<share::schema::ObColDesc> &get_col_descs() const = 0;
  virtual int serialize(char *buf, int64_t buf_len, int64_t &pos) const = 0;
  virtual int deserialize(const char *buf, int64_t data_len, int64_t &pos) = 0;
  virtual int64_t get_serialize_size() const = 0;
  virtual int64_t to_string(char *buf, int64_t buf_len) const = 0;

  virtual uint64_t get_table_id() const = 0;
  virtual int64_t get_schema_version() const = 0;
  virtual share::schema::ObIndexType get_index_type() const = 0;
  virtual int64_t get_rowkey_column_num() const = 0;
  virtual int64_t get_data_table_rowkey_column_num() const = 0;
  virtual int64_t get_fulltext_col_id() const = 0;
  virtual int get_typed_doc_id_col_id(uint64_t &doc_id_col_id, ObDocIDType &type) const = 0;
  virtual const common::ObString &get_fts_parser_name() const = 0;
  virtual const common::ObString &get_fts_parser_property() const = 0;
  virtual const common::ObString &get_index_name() const = 0;
  virtual uint64_t get_spatial_geo_col_id() const = 0;
  virtual int64_t get_multivalue_col_id() const = 0;
  virtual int64_t get_multivalue_array_col_id() const = 0;
  virtual int64_t get_vec_id_col_id() const = 0;
  virtual int64_t get_vec_chunk_col_id() const = 0;
  virtual int64_t get_embedded_vec_col_id() const = 0;
  virtual int64_t get_vec_vector_col_id() const = 0;
  virtual common::ObString get_vec_index_param() const = 0;
  virtual bool has_async_index() const = 0;
  virtual bool can_read_index() const = 0;
  virtual bool is_storage_index_table() const = 0;
  virtual bool is_unique_index() const = 0;
  virtual bool is_domain_index() const = 0;
  virtual bool is_spatial_index() const = 0;
  virtual bool is_fts_index() const = 0;
  virtual bool is_fts_index_aux() const = 0;
  virtual bool is_fts_doc_word_aux() const = 0;
  virtual bool is_multivalue_index() const = 0;
  virtual bool is_multivalue_index_aux() const = 0;
  virtual bool is_index_local_storage() const = 0;
  virtual bool is_vector_delta_buffer() const = 0;
  virtual bool is_vector_index_id() const = 0;
  virtual bool is_vector_index() const = 0;
  virtual bool is_sparse_vector_index() const = 0;
  virtual bool is_ivf_vector_index() const = 0;
  virtual bool is_hybrid_vector_index() const = 0;
  virtual bool is_hybrid_vector_index_log() const = 0;
  virtual bool is_no_need_update_vector_index() const = 0;
};

// Read-only query vocabulary for a table write plan.  It intentionally starts
// broad so callers can cross the boundary without changing DML behaviour; the
// surface can then be reduced without exposing the storage-owned schema object.
class ObDmlTableView
{
public:
  ObDmlTableView() : state_(nullptr) {}

  bool is_valid() const;
  uint64_t get_table_id() const;
  int64_t get_schema_version() const;
  share::schema::ObIndexType get_index_type() const;
  int64_t get_rowkey_column_num() const;
  int64_t get_data_table_rowkey_column_num() const;
  int64_t get_fulltext_col_id() const;
  int get_typed_doc_id_col_id(uint64_t &doc_id_col_id, ObDocIDType &type) const;
  const common::ObString &get_fts_parser_name() const;
  const common::ObString &get_fts_parser_property() const;
  const common::ObString &get_index_name() const;
  uint64_t get_spatial_geo_col_id() const;
  int64_t get_multivalue_col_id() const;
  int64_t get_multivalue_array_col_id() const;
  int64_t get_vec_id_col_id() const;
  int64_t get_vec_chunk_col_id() const;
  int64_t get_embedded_vec_col_id() const;
  int64_t get_vec_vector_col_id() const;
  common::ObString get_vec_index_param() const;
  bool has_async_index() const;
  bool can_read_index() const;
  bool is_storage_index_table() const;
  bool is_unique_index() const;
  bool is_domain_index() const;
  bool is_spatial_index() const;
  bool is_fts_index() const;
  bool is_fts_index_aux() const;
  bool is_fts_doc_word_aux() const;
  bool is_multivalue_index() const;
  bool is_multivalue_index_aux() const;
  bool is_index_local_storage() const;
  bool is_vector_delta_buffer() const;
  bool is_vector_index_id() const;
  bool is_vector_index() const;
  bool is_sparse_vector_index() const;
  bool is_ivf_vector_index() const;
  bool is_hybrid_vector_index() const;
  bool is_hybrid_vector_index_log() const;
  bool is_no_need_update_vector_index() const;

  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  explicit ObDmlTableView(const ObIDmlTablePlanState *state) : state_(state) {}
  friend class ObDmlTablePlan;

private:
  const ObIDmlTablePlanState *state_;
};

// Opaque, allocator-owned write plan.  The concrete storage schema remains a
// data-plane implementation detail while the wire representation stays
// compatible with the legacy plan during the migration.
class ObDmlTablePlan
{
public:
  explicit ObDmlTablePlan(common::ObIAllocator &allocator);
  ~ObDmlTablePlan();

  void reset();
  bool is_valid() const;
  int build(const share::schema::ObTableSchema *table_schema,
            const int64_t tenant_schema_version,
            const common::ObIArray<uint64_t> &column_ids);
  int set_data_table_rowkey_tags(share::schema::ObSchemaGetterGuard *guard,
                                 const share::schema::ObTableSchema *index_schema);
  int configure_multivalue_index(const int64_t data_table_rowkey_column_num);
  void set_has_async_index(const bool has_async_index);

  ObDmlTableView get_data_table() const;
  const common::ObIArray<share::schema::ObColDesc> &get_col_descs() const;

  // These delegate byte-for-byte to the storage implementation so existing
  // serialized DAS plans remain compatible.
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;
  int64_t to_string(char *buf, const int64_t buf_len) const;

private:
  ObDmlTablePlan(const ObDmlTablePlan &) = delete;
  ObDmlTablePlan &operator=(const ObDmlTablePlan &) = delete;

  ObIDmlTablePlanState *state() const { return state_; }
  friend class ObDmlTablePlanAccess;

private:
  common::ObIAllocator &allocator_;
  ObIDmlTablePlanState *state_;
};

// Typed access for data-plane adapters.  It reveals only the public abstract
// protocol, never a concrete storage object.
class ObDmlTablePlanAccess
{
public:
  static ObIDmlTablePlanState *state(const ObDmlTablePlan &plan)
  {
    return plan.state();
  }
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_DML_TABLE_PLAN_H_
