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

#define USING_LOG_PREFIX PL

#include "ob_pl_user_type.h"
#include "query/protocol/ob_mysql_protocol_util.h"
#include "pl/ob_pl_package.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace obmysql;
using namespace sql;

namespace pl
{
int64_t ObUserDefinedType::get_member_count() const
{
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

const ObPLDataType *ObUserDefinedType::get_member(int64_t i) const
{
  UNUSEDx(i);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return NULL;
}




int ObUserDefinedType::get_size(
  ObPLTypeSize type, int64_t &size) const
{
  UNUSEDx(type, size);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::init_session_var(
  const ObPLResolveCtx &resolve_ctx, common::ObIAllocator &obj_allocator,
  sql::ObExecContext &exec_ctx, const sql::ObSqlExpression *default_expr, bool default_construct,
  common::ObObj &obj) const
{
  UNUSEDx(resolve_ctx, obj_allocator, exec_ctx, default_expr, default_construct, obj);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::add_package_routine_schema_param(
  const ObPLResolveCtx &resolve_ctx, const ObPLBlockNS &block_ns,
  const common::ObString &package_name, const common::ObString &param_name,
  int64_t mode, int64_t position, int64_t level, int64_t &sequence,
  share::schema::ObRoutineInfo &routine_info) const
{
  UNUSEDx(
    resolve_ctx, block_ns,package_name,
    param_name, mode, position, level, sequence, routine_info);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::get_all_depended_user_type(
  const ObPLResolveCtx &resolve_ctx, const ObPLBlockNS &current_ns) const
{
  UNUSEDx(resolve_ctx, current_ns);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::init_obj(
  share::schema::ObSchemaGetterGuard &schema_guard, common::ObIAllocator &allocator,
  common::ObObj &obj, int64_t &init_size) const
{
  UNUSEDx(schema_guard, allocator, obj, init_size);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::serialize(
  share::schema::ObSchemaGetterGuard &schema_guard,
  const sql::ObSQLSessionInfo &session,
  const common::ObTimeZoneInfo *tz_info, obmysql::MYSQL_PROTOCOL_TYPE type,
  char *&src, char *dst, const int64_t dst_len, int64_t &dst_pos) const
{
  UNUSEDx(schema_guard, session, tz_info, type, src, dst, dst_len, dst_pos);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::deserialize(
  share::schema::ObSchemaGetterGuard &schema_guard, common::ObIAllocator &allocator, sql::ObSQLSessionInfo *session,
  const common::ObCharsetType charset, const common::ObCollationType cs_type, const common::ObTimeZoneInfo *tz_info,
  const char *&src, char *dst, const int64_t dst_len, int64_t &dst_pos) const
{
  UNUSEDx(
    schema_guard, allocator, session, charset, cs_type, tz_info, src, dst, dst_len, dst_pos);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::convert(ObPLResolveCtx &ctx, ObObj *&src, ObObj *&dst) const
{
  UNUSEDx(ctx, src, dst);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::deep_copy(common::ObIAllocator &alloc, const ObUserDefinedType &other)
{
  int ret = OB_SUCCESS;
  OZ (ObPLDataType::deep_copy(alloc, other));
  OZ (ob_write_string(alloc, other.get_name(), type_name_));
  return ret;
}




int ObUserDefinedType::newx(common::ObIAllocator &allocator, const ObPLINS *ns, int64_t &ptr) const
{
  int ret = OB_NOT_SUPPORTED;
  UNUSEDx(allocator, ns, ptr);
  LOG_WARN("Unexpected type to nex", K(ret));
  return ret;
}

int ObUserDefinedType::deep_copy_obj(
  ObIAllocator &allocator, const ObObj &src, ObObj &dst, bool need_new_allocator, bool ignore_del_element)
{
  int ret = OB_SUCCESS;
  CK (src.is_pl_extend());

  if (OB_SUCC(ret)) {
    switch (src.get_meta().get_extend_type()) {
    case PL_CURSOR_TYPE: {
      OZ (ObPLCursorType::deep_copy_cursor(allocator, src, dst));
    }
      break;
    case PL_RECORD_TYPE: {
      OZ (ObPLComposite::copy_element(src, dst, allocator, NULL, NULL, NULL,  need_new_allocator, ignore_del_element));
    }
      break;

    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected type to deep copy", K(src), K(ret), K(src.get_meta().get_extend_type()));
    }
      break;
    }
  }
  return ret;
}

int ObUserDefinedType::destruct_objparam(ObIAllocator &alloc, ObObj &src, ObSQLSessionInfo *session, bool direct_use_alloc)
{
  int ret = OB_SUCCESS;

  if (src.is_pl_extend()) {
    int8_t extend_type = src.get_meta().get_extend_type();
    if (PL_RECORD_TYPE == extend_type ||
        PL_NESTED_TABLE_TYPE == extend_type ||
        PL_ASSOCIATIVE_ARRAY_TYPE == extend_type ||
        PL_VARRAY_TYPE == extend_type) {
      ObPLAllocator1 *pl_allocator = nullptr;
      ObIAllocator *parent_allocator = nullptr;
      ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(src.get_ext());
      if (direct_use_alloc) {
        ObIAllocator *allocator = nullptr;
        OV (OB_NOT_NULL(composite), OB_ERR_UNEXPECTED, lbt());
        OX (allocator = composite->get_allocator());
        OZ (SMART_CALL(ObUserDefinedType::destruct_obj(src, session)));
        if (OB_SUCC(ret) && OB_NOT_NULL(allocator)) {
          alloc.free(allocator);
          composite->set_allocator(nullptr);
        }
        OX (alloc.free(composite));
      } else {
        OV (OB_NOT_NULL(composite), OB_ERR_UNEXPECTED, lbt());
        OV (OB_NOT_NULL(composite->get_allocator()), OB_ERR_UNEXPECTED, lbt());
        OX (pl_allocator = dynamic_cast<ObPLAllocator1 *>(composite->get_allocator()));
        CK (OB_NOT_NULL(pl_allocator));
        CK (OB_NOT_NULL(parent_allocator = pl_allocator->get_parent_allocator()));
        OZ (SMART_CALL(ObUserDefinedType::destruct_obj(src, session)));
        //CK (parent_allocator == &alloc);
        OX (parent_allocator->free(pl_allocator));
        OX (composite->set_allocator(nullptr));
        OX (parent_allocator->free(composite));
      }
    } else {
      OZ (SMART_CALL(ObUserDefinedType::destruct_obj(src, session)));
    }
  } else {
    void *ptr = src.get_deep_copy_obj_ptr();
    if (nullptr != ptr) {
      alloc.free(ptr);
    }
  }
  src.set_null();

  return ret;
}

int ObUserDefinedType::reset_composite(ObObj &value, ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  CK (value.is_pl_extend());
  if (OB_SUCC(ret)) {
    if (PL_RECORD_TYPE == value.get_meta().get_extend_type()) {
      OZ (ObUserDefinedType::reset_record(value, session));
    } else {
      OZ (ObUserDefinedType::destruct_obj(value, session, true));
    }
  }

  return ret;
}

int ObUserDefinedType::reset_record(ObObj &src, ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;

  ObPLRecord *record = reinterpret_cast<ObPLRecord*>(src.get_ext());
  CK (OB_NOT_NULL(record));
  if (OB_SUCC(ret) && OB_NOT_NULL(record->get_allocator())) {
    ObPLAllocator1 *pl_allocator = dynamic_cast<ObPLAllocator1 *>(record->get_allocator());
    CK (OB_NOT_NULL(pl_allocator));
    for (int64_t i = 0; OB_SUCC(ret) && i < record->get_count(); ++i) {
      ObObj &obj = record->get_element()[i];
      if (obj.is_pl_extend()) {
        int8_t extend_type = obj.get_meta().get_extend_type();
        if (PL_RECORD_TYPE == extend_type) {
          OZ (SMART_CALL(reset_record(obj, session)));
        } else if (PL_NESTED_TABLE_TYPE == extend_type ||
                  PL_ASSOCIATIVE_ARRAY_TYPE == extend_type ||
                  PL_VARRAY_TYPE == extend_type) {
          OZ (SMART_CALL(destruct_obj(obj, session, true)));
        } else {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected type", K(ret), K(obj), K(extend_type), KPC(record));
        }
      } else {
        OZ (SMART_CALL(destruct_objparam(*pl_allocator, obj, session, true)));
      }
    }
  }

  return ret;
}
// keep_composite_attr = true, retain its allocator attribute, for record, retain the data field
// Otherwise, all memory is cleaned
int ObUserDefinedType::destruct_obj(ObObj &src, ObSQLSessionInfo *session, bool keep_composite_attr)
{
  int ret = OB_SUCCESS;

  if (src.is_pl_extend() && src.get_ext() != 0) {
    switch (src.get_meta().get_extend_type()) {
    case PL_CURSOR_TYPE: {
      ObPLCursorInfo *cursor = reinterpret_cast<ObPLCursorInfo*>(src.get_ext());
      CK (OB_NOT_NULL(cursor));
      CK (OB_NOT_NULL(session));
      OZ (cursor->close(*session));
      OX (cursor->~ObPLCursorInfo());
      OX (src.set_null());
    }
      break;
    case PL_RECORD_TYPE: {
      ObPLRecord *record = reinterpret_cast<ObPLRecord*>(src.get_ext());
      CK  (OB_NOT_NULL(record));
      if (OB_SUCC(ret) && OB_NOT_NULL(record->get_allocator())) {
        ObPLAllocator1 *pl_allocator = dynamic_cast<ObPLAllocator1 *>(record->get_allocator());
        CK (OB_NOT_NULL(pl_allocator));
        for (int64_t i = 0; OB_SUCC(ret) && i < record->get_count(); ++i) {
          ObObj &obj = record->get_element()[i];
          OZ (SMART_CALL(destruct_objparam(*pl_allocator, obj, session, true)));
          new(&obj)ObObj();
        }
      }
      if (OB_SUCC(ret)) {
        common::ObIAllocator *record_allocator = record->get_allocator();
        if (NULL == record_allocator) {
          //The allocator for Record that was only defined but never used is empty, this is normal, skip it
          LOG_DEBUG("Notice: a record declared but not used", K(src), K(ret));
        } else {
          ObPLAllocator1 *pl_allocator = dynamic_cast<ObPLAllocator1 *>(record_allocator);
          if (NULL == pl_allocator) {
            ret = OB_ERR_UNEXPECTED;
            LOG_ERROR("here must be a bug!!!", K(record_allocator), K(ret));
          } else if (!pl_allocator->is_inited()) {
            // do nothing
          } else if (!keep_composite_attr) {
            common::ObIAllocator *parent_allocator = pl_allocator->get_parent_allocator();
            CK (OB_NOT_NULL(parent_allocator));
            if (OB_SUCC(ret)) {
              pl_allocator->free(record->get_element());
              //pl_allocator->reset();
              pl_allocator->~ObPLAllocator1();
              //parent_allocator->free(pl_allocator);
              record->set_allocator(nullptr);
              record->set_data(nullptr);
              record->set_count(0);
              //parent_allocator->free(record);
            }
          } else {
            OX (record->set_null());
          }
        }
      }
    }
      break;
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected type to destruct", K(src), K(src.get_meta().get_extend_type()), K(ret));
    }
       break;
    }
  } else {
    //do nothing and return
  }
  return ret;
}

int ObUserDefinedType::alloc_sub_composite(ObObj &dest_element, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;

#define COPY_SUB_COLLECTION(TYPE) \
  do {  \
    if (OB_ISNULL(dest_composite = reinterpret_cast<ObPLComposite*>(allocator.alloc(old_composite->get_init_size())))) {  \
      ret = OB_ALLOCATE_MEMORY_FAILED;                                \
      LOG_WARN("failed to alloc memory for collection", K(ret));      \
    } else {                                                          \
      TYPE *collection = static_cast<TYPE*>(dest_composite);                    \
      CK (OB_NOT_NULL(collection));                                   \
      LOG_INFO("src is: ", KP(old_composite), KP(dest_composite), K(old_composite->get_init_size()));                                   \
      OX (new(collection)TYPE(old_composite->get_id()));                         \
      OZ (collection->init_allocator(allocator, false));  \
      if (OB_FAIL(ret)) {    \
        allocator.free(dest_composite);     \
      }    \
    }     \
  } while (0)

  if (dest_element.is_ext() && dest_element.get_meta().get_extend_type() != PL_OPAQUE_TYPE) {
    ObPLComposite *old_composite = reinterpret_cast<ObPLComposite*>(dest_element.get_ext());
    ObPLComposite *dest_composite = nullptr;
    CK (OB_NOT_NULL(old_composite));
    if (OB_SUCC(ret)) {
      switch (old_composite->get_type()) {
        case PL_RECORD_TYPE: {
          ObPLRecord *composite = NULL;
          dest_composite = reinterpret_cast<ObPLComposite*>(allocator.alloc(old_composite->get_init_size()));
          composite = static_cast<ObPLRecord*>(dest_composite);
          int64_t record_count = static_cast<ObPLRecord*>(old_composite)->get_count();
          if (OB_ISNULL(composite)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate composite memory failed", K(ret));
          }
          OX (new(composite)ObPLRecord(old_composite->get_id(), record_count));
          OZ (composite->init_data(allocator, false));
          if (OB_FAIL(ret) && OB_NOT_NULL(composite)) {
            allocator.free(composite);
          }
        }
          break;
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Unexpected type to destruct", K(dest_element), K(dest_element.get_meta().get_extend_type()), K(ret));
        }
          break;
      }
      OX (dest_element.set_extend(reinterpret_cast<int64_t>(dest_composite),
                                    dest_element.get_meta().get_extend_type(),
                                    dest_element.get_val_len()));
    }
  }
#undef COPY_SUB_COLLECTION
  return ret;
}


int ObUserDefinedType::serialize_obj(const ObObj &obj, char* buf, const int64_t len, int64_t& pos)
{
  int ret = OB_SUCCESS;
  CK (obj.is_pl_extend());
  OZ (serialization::encode(buf, len, pos, SERVER_CURRENT_VERSION));
  OZ (serialization::encode(buf, len, pos, obj.get_meta().get_extend_type()));
  if (OB_SUCC(ret)) {
    switch (obj.get_meta().get_extend_type()) {
    case PL_RECORD_TYPE: {
      //todo:
      ret = OB_NOT_SUPPORTED;
    }
      break;
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected type to serialize", K(obj), K(ret));
    }
      break;
    }
  }
  return ret;
}

int ObUserDefinedType::deserialize_obj(ObObj &obj, const char* buf, const int64_t len, int64_t& pos)
{
  int ret = OB_SUCCESS;
  int64_t version = OB_INVALID_VERSION;
  uint8_t pl_type = PL_INVALID_TYPE;
  uint64_t id = OB_INVALID_ID;
  OZ (serialization::decode(buf, len, pos, version));
  if (OB_SUCC(ret) && OB_UNLIKELY(DATA_CURRENT_VERSION != static_cast<uint64_t>(version))) {
    ret = OB_VERSION_NOT_MATCH;
    LOG_WARN("PL user type data format version does not match",
             KR(ret), K(version), "expected_version", DATA_CURRENT_VERSION);
  }
  OZ (serialization::decode(buf, len, pos, pl_type));
  OZ (serialization::decode(buf, len, pos, id));
  if (OB_SUCC(ret)) {
    switch (pl_type) {
    case PL_RECORD_TYPE: {
      //todo:
      ret = OB_NOT_SUPPORTED;
    }
      break;
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected type to deserialize", K(obj), K(ret));
    }
      break;
    }
  }

  return ret;
}

int64_t ObUserDefinedType::get_serialize_obj_size(const ObObj &obj)
{
  int64_t size = 0;
  int ret = OB_SUCCESS;
  CK (obj.is_pl_extend());
  OX (size += serialization::encoded_length(SERVER_CURRENT_VERSION));
  OX (size += serialization::encoded_length(obj.get_meta().get_extend_type()));
  if (OB_SUCC(ret)) {
    switch (obj.get_meta().get_extend_type()) {
    case PL_RECORD_TYPE: {
      //todo:
      ret = OB_NOT_SUPPORTED;
    }
      break;
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("Unexpected type to get serialize size", K(obj), K(ret));
    }
      break;
    }
  }
  return size;
}



//---------- for ObPLCursorType ----------

int ObPLCursorType::deep_copy(common::ObIAllocator &alloc, const ObPLCursorType &other)
{
  int ret = OB_SUCCESS;
  OZ (ObUserDefinedType::deep_copy(alloc, other));
  return ret;
}



int ObPLCursorType::newx(common::ObIAllocator &allocator, const ObPLINS *ns, int64_t &ptr) const
{
  int ret = OB_NOT_SUPPORTED;
  UNUSEDx(allocator, ns, ptr);
  return ret;
}

int ObPLCursorType::get_size(ObPLTypeSize type, int64_t &size) const
{
  UNUSEDx(type, size);
  size = sizeof(ObPLCursorInfo) + 8;
  return OB_SUCCESS;
}

int ObPLCursorType::init_obj(ObSchemaGetterGuard &schema_guard,
                              ObIAllocator &allocator,
                              ObObj &obj,
                              int64_t &init_size) const
{
  int ret = OB_SUCCESS;
  char *data = NULL;
  init_size = 0;
  if (obj.is_ext()){
    data = reinterpret_cast<char *>(obj.get_ext());
  }
  if (OB_NOT_NULL(data)) {
    MEMSET(data, 0, init_size);
    new(data) ObPLCursorInfo(&allocator);
    obj.set_ext(reinterpret_cast<int64_t>(data));
  } else if (OB_FAIL(get_size(PL_TYPE_INIT_SIZE, init_size))) {
    LOG_WARN("get init size failed", K(ret));
  } else if (OB_ISNULL(data = static_cast<char *>(allocator.alloc(init_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("memory allocate failed", K(ret));
  } else {
    MEMSET(data, 0, init_size);
    new(data) ObPLCursorInfo(&allocator);
    obj.set_extend(reinterpret_cast<int64_t>(data), PL_CURSOR_TYPE);
  }
  return ret;
}

int ObPLCursorType::init_session_var(const ObPLResolveCtx &resolve_ctx,
                                      ObIAllocator &obj_allocator,
                                      sql::ObExecContext &exec_ctx,
                                      const sql::ObSqlExpression *default_expr,
                                      bool default_construct,
                                      ObObj &obj) const
{
  UNUSEDx(exec_ctx, default_expr, default_construct);
  int ret = OB_SUCCESS;
  char *data = NULL;
  int64_t init_size = 0;
  if (OB_FAIL(get_size(PL_TYPE_INIT_SIZE, init_size))) {
    LOG_WARN("get init size failed", K(ret));
  } else if (OB_ISNULL(data = static_cast<char *>(obj_allocator.alloc(init_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("memory allocate failed", K(ret));
  } else {
    MEMSET(data, 0, init_size);
    obj.set_extend(reinterpret_cast<int64_t>(data), PL_CURSOR_TYPE);
  }
  return ret;
}

int ObPLCursorType::deep_copy_cursor(common::ObIAllocator &allocator,
                                   const ObObj &src,
                                   ObObj &dest)
{
  int ret = OB_SUCCESS;
  ObPLCursorInfo *src_cursor = NULL;
  ObPLCursorInfo *dest_cursor = NULL;
  if (0 == dest.get_ext()) {
    OZ (ObSPIService::spi_cursor_alloc(allocator, dest));
  }
  OX (src_cursor = reinterpret_cast<ObPLCursorInfo*>(src.get_ext()));
  OX (dest_cursor = reinterpret_cast<ObPLCursorInfo*>(dest.get_ext()));
  CK (OB_NOT_NULL(src_cursor));
  CK (OB_NOT_NULL(dest_cursor));
  OZ (dest_cursor->deep_copy(*src_cursor, &allocator));
  return ret;
}

//---------- for ObRecordType ----------

// int ObRecordMember::deep_copy_default_expr(const ObRecordMember &member,
//                                            ObIAllocator &allocator,
//                                            ObRawExprFactory &expr_factory,
//                                            bool deep_copy_expr)
// {
//   UNUSED(allocator);
//   int ret = OB_SUCCESS;
//   // first copy the default expr, later will check need deep copy
//   ObRawExpr *expr = member.get_default_expr();
//   if (OB_INVALID_INDEX == member.get_default() || OB_ISNULL(member.get_default_expr())) {
//     // do nothing
//   } else if (deep_copy_expr && ObPLExprCopier::copy_expr(expr_factory,
//                                                          member.get_default_expr(),
//                                                          expr)) {
//     LOG_WARN("copy raw expr failed", K(ret));
//   } else {
//     default_expr_ = 0;
//     default_raw_expr_ = expr;
//   }
//   return ret;
// }

//---------- for ObRecordType ----------

int ObRecordType::record_members_init(common::ObIAllocator *alloc, int64_t size)
{
  int ret = OB_SUCCESS;
  record_members_.set_allocator(alloc);
  if (OB_FAIL(record_members_.init(size))) {
    LOG_WARN("failed to init record_members_ count", K(ret));
  }

  return ret;
}


int ObRecordType::add_record_member(const ObRecordMember &record)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(record_members_.count() >= MAX_RECORD_COUNT)) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("record member count is too many", K(record_members_.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
      if (common::ObCharset::case_insensitive_equal(
        record_members_.at(i).member_name_, record.member_name_)) {
        ret = OB_ENTRY_EXIST;
        LOG_WARN("dup record member found", K(ret), K(record.member_name_), K(i));
        break;
      }
    }
    OZ (record_members_.push_back(record));
  }
  return ret;
}

int ObRecordType::add_record_member(const ObString &record_name,
                                    const ObPLDataType &record_type,
                                    int64_t default_idx,
                                    sql::ObRawExpr *default_raw_expr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(record_members_.count() >= MAX_RECORD_COUNT)) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("record member count is too many", K(record_members_.count()));
  } else if (record_type.get_not_null() && OB_INVALID_INDEX == default_idx) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("record member with not null modifier must hava default value", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
      if (common::ObCharset::case_insensitive_equal(
        record_members_.at(i).member_name_, record_name)) {
        ret = OB_ENTRY_EXIST;
        LOG_WARN("dup record member found", K(ret), K(record_name), K(i));
        break;
      }
    }
    OZ (record_members_.push_back(ObRecordMember(
      record_name, record_type, default_idx, default_raw_expr)));
  }
  return ret;
}

//not the same enum_set_ctx
int ObRecordType::add_record_member(ObPLEnumSetCtx &enum_set_ctx,
                                    const ObString &record_name,
                                    const ObPLDataType &record_type,
                                    int64_t default_idx,
                                    sql::ObRawExpr *default_raw_expr)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(record_members_.count() >= MAX_RECORD_COUNT)) {
    ret = OB_BUF_NOT_ENOUGH;
    LOG_ERROR("record member count is too many", K(record_members_.count()));
  } else if (record_type.get_not_null() && OB_INVALID_INDEX == default_idx) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("record member with not null modifier must hava default value", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
      if (common::ObCharset::case_insensitive_equal(
        record_members_.at(i).member_name_, record_name)) {
        ret = OB_ENTRY_EXIST;
        LOG_WARN("dup record member found", K(ret), K(record_name), K(i));
        break;
      }
    }
    ObPLDataType member_type;
    OZ (member_type.deep_copy(enum_set_ctx, record_type));
    OZ (record_members_.push_back(ObRecordMember(
                                 record_name, member_type, default_idx, default_raw_expr)));
  }
  return ret;
}


int64_t ObRecordType::get_record_member_index(const ObString &record_name) const
{
  int64_t index = OB_INVALID_INDEX;
  for (int64_t i = 0; i < record_members_.count(); ++i) {
    if (common::ObCharset::case_insensitive_equal(
        record_members_.at(i).member_name_, record_name)) {
      index = i;
      break;
    }
  }
  return index;
}

const ObPLDataType *ObRecordType::get_record_member_type(int64_t index) const
{
  const ObPLDataType *type = NULL;
  if (OB_LIKELY(index >= 0) && OB_LIKELY(index < record_members_.count())) {
    type = &record_members_.at(index).member_type_;
  }
  return type;
}

const ObString *ObRecordType::get_record_member_name(int64_t index) const
{
  const ObString *type = NULL;
  if (OB_LIKELY(index >= 0) && OB_LIKELY(index < record_members_.count())) {
    type = &record_members_.at(index).member_name_;
  }
  return type;
}

const ObRecordMember *ObRecordType::get_record_member(int64_t index) const
{
  const ObRecordMember *record_member = NULL;
  if (OB_LIKELY(index >= 0) && OB_LIKELY(index < record_members_.count())) {
    record_member = &record_members_.at(index);
  }
  return record_member;
}


int ObRecordType::is_compatble(const ObRecordType &other, bool &is_comp) const
{
  int ret = OB_SUCCESS;
  is_comp = true;
  if (get_record_member_count() != other.get_record_member_count()) {
    is_comp = false;
    LOG_TRACE("record type is not compatible",
              K(get_record_member_count()), K(other.get_record_member_count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && is_comp && i < get_record_member_count(); ++i) {
      const ObPLDataType *left = get_record_member_type(i);
      const ObPLDataType *right = other.get_record_member_type(i);
      CK (OB_NOT_NULL(left));
      CK (OB_NOT_NULL(right));
      LOG_TRACE("check record member type", K(i), KPC(left), KPC(right));
      if (OB_SUCC(ret)) {
        if (left->is_obj_type() && right->is_obj_type()) {
          CK (OB_NOT_NULL(left->get_data_type()));
          CK (OB_NOT_NULL(right->get_data_type()));
          OX (is_comp = cast_supported(left->get_data_type()->get_obj_type(),
                                      left->get_data_type()->get_collation_type(),
                                      right->get_data_type()->get_obj_type(),
                                      right->get_data_type()->get_collation_type()));
          LOG_TRACE("check obj type cast support",
                    K(i), K(is_comp), KPC(left->get_data_type()), KPC(right->get_data_type()));
        } else if ((!left->is_obj_type() ||
                    (left->get_data_type() != NULL && left->get_data_type()->get_meta_type().is_ext()))
                      &&
                    (!right->is_obj_type() ||
                    (right->get_data_type() != NULL && right->get_data_type()->get_meta_type().is_ext()))) {
          uint64_t left_udt_id = (NULL == left->get_data_type()) ? left->get_user_type_id()
                                                                  : left->get_data_type()->get_udt_id();
          uint64_t right_udt_id = (NULL == right->get_data_type()) ? right->get_user_type_id()
                                                                    : right->get_data_type()->get_udt_id();
          if (left_udt_id != right_udt_id) {
            is_comp = false;
            LOG_TRACE("record type is not compatible", K(i), K(left_udt_id), K(right_udt_id));
          }
        } else {
          is_comp = false;
        }
      }
    }
  }
  return ret;
}

int64_t ObRecordType::get_notnull_offset()
{
  return sizeof(ObPLRecord);
}

int64_t ObRecordType::get_meta_offset(int64_t count)
{
  return ObRecordType::get_notnull_offset() + 8 * ((count - 1) / 8 + 1); // notnull is bool, needs alignment
}

int64_t ObRecordType::get_data_offset(int64_t count)
{
  return ObRecordType::get_meta_offset(count) + sizeof(ObDataType) * count;
}

int64_t ObRecordType::get_init_size(int64_t count)
{
  return ObRecordType::get_data_offset(count);
}

int ObRecordType::deep_copy(
  common::ObIAllocator &alloc, const ObRecordType &other, bool shadow_copy)
{
  int ret = OB_SUCCESS;
  OZ (ObUserDefinedType::deep_copy(alloc, other));
  OZ (record_members_init(&alloc, other.get_record_member_count()));
  for (int64_t i = 0; OB_SUCC(ret) && i < other.get_record_member_count(); i++) {
    const ObRecordMember *record_member = other.get_record_member(i);
    ObString new_member_name;
    OZ (ob_write_string(alloc, record_member->member_name_, new_member_name));
    OZ (add_record_member(new_member_name,
                          record_member->member_type_,
                          record_member->default_expr_,
                          shadow_copy ? record_member->default_raw_expr_ : NULL));
  } 
  return ret;
}

//not the same enum_set_ctx
int ObRecordType::deep_copy(
  ObPLEnumSetCtx &enum_set_ctx, common::ObIAllocator &alloc, const ObRecordType &other, bool shadow_copy)
{
  int ret = OB_SUCCESS;
  OZ (ObUserDefinedType::deep_copy(alloc, other));
  OZ (record_members_init(&alloc, other.get_record_member_count()));
  for (int64_t i = 0; OB_SUCC(ret) && i < other.get_record_member_count(); i++) {
    const ObRecordMember *record_member = other.get_record_member(i);
    ObString new_member_name;
    OZ (ob_write_string(alloc, record_member->member_name_, new_member_name));
    OZ (add_record_member(enum_set_ctx,
                          new_member_name,
                          record_member->member_type_,
                          record_member->default_expr_,
                          shadow_copy ? record_member->default_raw_expr_ : NULL));
  }
  return ret;
}





int ObRecordType::newx(common::ObIAllocator &allocator, const ObPLINS *ns, int64_t &ptr) const
{
  int ret = OB_SUCCESS;
  ObPLRecord *record = NULL;
  ObObj *member = NULL;
  int64_t init_size = ObRecordType::get_init_size(get_member_count());
  record = reinterpret_cast<ObPLRecord*>(allocator.alloc(init_size));
  if (OB_ISNULL(record)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc record failed", K(ret));
  }
  OX (new (record)ObPLRecord(user_type_id_, get_member_count()));
  OZ (record->init_data(allocator, false));
  OX (ptr = reinterpret_cast<int64_t>(record));
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < get_member_count(); ++i) {
      CK (OB_NOT_NULL(get_member(i)));
      OZ (record->get_element(i, member));
      CK (OB_NOT_NULL(member));
      if (get_member(i)->is_obj_type()) {
        OX (new (member) ObObj(ObNullType));
      } else {
        int64_t init_size = OB_INVALID_SIZE;
        int64_t member_ptr = 0;
        OZ (get_member(i)->get_size(PL_TYPE_INIT_SIZE, init_size));
        OZ (get_member(i)->newx(*record->get_allocator(), ns, member_ptr));
        OX (member->set_extend(member_ptr, get_member(i)->get_type(), init_size));
      }
    }
    if (OB_FAIL(ret)) {
      ObObj tmp;
      tmp.set_extend(ptr, this->get_type(), init_size);
      ObUserDefinedType::destruct_objparam(allocator, tmp, nullptr);
      ptr = 0;
    }
  } else if (OB_NOT_NULL(record)) {
    allocator.free(record);
  }
  return ret;
}



int ObRecordType::get_size(ObPLTypeSize type, int64_t &size) const
{
  int ret = OB_SUCCESS;
  size = get_data_offset(get_record_member_count());
  return ret;
}

int ObRecordType::init_session_var(const ObPLResolveCtx &resolve_ctx,
                                   ObIAllocator &obj_allocator,
                                   sql::ObExecContext &exec_ctx,
                                   const sql::ObSqlExpression *default_expr,
                                   bool default_construct,
                                   ObObj &obj) const
{
  UNUSEDx(exec_ctx, default_expr, default_construct);
  int ret = OB_SUCCESS;
  char *data = NULL;
  int64_t init_size = 0;
  ObArenaAllocator tmp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_INIT_SESSION_VAR), OB_MALLOC_NORMAL_BLOCK_SIZE);
  obj.set_null();
  if (OB_NOT_NULL(default_expr)) {
    ObObj calc_obj;
    OZ (ObSQLUtils::calc_sql_expression_without_row(exec_ctx, *default_expr, calc_obj, &tmp_allocator));
    CK (calc_obj.is_null() || calc_obj.is_pl_extend());
    if (OB_SUCC(ret) && calc_obj.is_pl_extend()) {
      OZ (ObUserDefinedType::deep_copy_obj(obj_allocator, calc_obj, obj));
    }
  }
  if (OB_FAIL(ret) || obj.is_pl_extend()) {
    // do nothing ...
  } else if (OB_FAIL(get_size(PL_TYPE_INIT_SIZE, init_size))) {
    LOG_WARN("get init size failed", K(ret));
  } else if (OB_ISNULL(data = static_cast<char *>(obj_allocator.alloc(init_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("memory allocate failed", K(ret));
  } else {
    ObPLRecord *record = reinterpret_cast<ObPLRecord*>(data);
    ObObj *member = NULL;
    MEMSET(data, 0, init_size);
    new (data) ObPLRecord(user_type_id_, record_members_.count());
    if (OB_FAIL(record->init_data(obj_allocator, true))) {
      obj_allocator.free(data);
    } else {
      obj.set_extend(reinterpret_cast<int64_t>(data), type_, init_size);
      for (int64_t i = 0; OB_SUCC(ret) && i < get_member_count(); ++i) {
        const ObRecordMember* record_member = get_record_member(i);
        const ObPLDataType* member_type = get_record_member_type(i);
        CK (OB_NOT_NULL(get_member(i)));
        OZ (record->get_element(i, member));
        CK (OB_NOT_NULL(member));
        CK (OB_NOT_NULL(record_member));
        CK (OB_NOT_NULL(member_type));
        if (OB_FAIL(ret)) { 
        } else if (record_member->get_default() != OB_INVALID_INDEX) {
          uint64_t package_id = extract_package_id(get_user_type_id());
          int64_t expr_idx = record_member->get_default();
          ObObjParam result;
          OV (is_package_type(), OB_ERR_UNEXPECTED, KPC(this));
          OV (package_id != OB_INVALID_ID, OB_ERR_UNEXPECTED, KPC(this));
          OV (expr_idx != OB_INVALID_INDEX, OB_ERR_UNEXPECTED, KPC(this));
          OZ (sql::ObSPIService::spi_calc_package_expr_v1(resolve_ctx, exec_ctx, tmp_allocator, package_id, expr_idx, &result));
          if (OB_FAIL(ret)) {
          } else if (result.is_pl_extend()) {
            ObObj tmp;
            OZ (ObUserDefinedType::deep_copy_obj(*record->get_allocator(), result, tmp, false));
            OX (result = tmp);
            OX (*member = tmp);
          } else if (result.is_null() && !get_member(i)->is_obj_type()) {
            int64_t init_size = OB_INVALID_SIZE;
            int64_t member_ptr = 0;
            OZ (get_member(i)->get_size(PL_TYPE_INIT_SIZE, init_size));
            OZ (get_member(i)->newx(*record->get_allocator(), &resolve_ctx, member_ptr));
            OX (member->set_extend(member_ptr, get_member(i)->get_type(), init_size));
            if (OB_SUCC(ret) && get_member(i)->is_record_type()) {
              ObPLComposite *composite = reinterpret_cast<ObPLComposite *>(member_ptr);
              CK (OB_NOT_NULL(composite));
              OX (composite->set_null());
            }
          } else {
            ObObj tmp;
            OZ (common::deep_copy_obj(*record->get_allocator(), result, tmp));
            OX (result = tmp);
            OX (*member = result);
          }
        } else {
          if (get_member(i)->is_obj_type()) {
            OX (new (member) ObObj(ObNullType));
          } else {
            int64_t init_size = OB_INVALID_SIZE;
            int64_t member_ptr = 0;
            OZ (get_member(i)->get_size(PL_TYPE_INIT_SIZE, init_size));
            OZ (get_member(i)->newx(*record->get_allocator(), &resolve_ctx, member_ptr));
            OX (member->set_extend(member_ptr, get_member(i)->get_type(), init_size));
          }
        }
      }
      if (OB_FAIL(ret)) {
        ObUserDefinedType::destruct_objparam(obj_allocator, obj, &(resolve_ctx.session_info_));
      }
    }
  }
  return ret;
}

int ObRecordType::add_package_routine_schema_param(const ObPLResolveCtx &resolve_ctx,
                                                   const ObPLBlockNS &block_ns,
                                                   const common::ObString &package_name,
                                                   const common::ObString &param_name,
                                                   int64_t mode, int64_t position,
                                                   int64_t level, int64_t &sequence,
                                                   share::schema::ObRoutineInfo &routine_info) const
{
  int ret = OB_SUCCESS;
  UNUSEDx(param_name, position);
  for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
    const ObRecordMember* record_member = get_record_member(i);
    const ObPLDataType &type = record_member->member_type_;
    OZ (type.add_package_routine_schema_param(
        resolve_ctx, block_ns, package_name, record_member->member_name_,
        mode, i+1, level+1, sequence, routine_info), KPC(this));
  }
  return ret;
}

int ObRecordType::get_all_depended_user_type(const ObPLResolveCtx &resolve_ctx,
                                             const ObPLBlockNS &current_ns) const
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
    const ObRecordMember* record_member = get_record_member(i);
    const ObPLDataType &type = record_member->member_type_;
    if (OB_FAIL(type.get_all_depended_user_type(resolve_ctx, current_ns))) {
       LOG_WARN("failed to add user type", K(*this), K(ret));
    }
  }
  return ret;
}

int ObRecordType::init_obj(ObSchemaGetterGuard &schema_guard,
                           ObIAllocator &allocator,
                           ObObj &obj,
                           int64_t &init_size) const
{
  int ret = OB_SUCCESS;
  char *data = NULL;
  init_size = 0;
  if (OB_FAIL(get_size(PL_TYPE_INIT_SIZE, init_size))) {
    LOG_WARN("get init size failed", K(ret));
  } else if (OB_ISNULL(data = static_cast<char *>(allocator.alloc(init_size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("memory allocate failed", K(ret));
  } else {
    ObPLRecord *record = reinterpret_cast<ObPLRecord*>(data);
    MEMSET(data, 0, init_size);
    new (data) ObPLRecord(get_user_type_id(), get_record_member_count());
    OZ (record->init_data(allocator, true));
    if (OB_FAIL(ret)) {
      allocator.free(data);
    } else {
      OX (obj.set_extend(reinterpret_cast<int64_t>(data), type_, init_size));
    }
  }
  return ret;
}

int ObRecordType::serialize(share::schema::ObSchemaGetterGuard &schema_guard,
                            const sql::ObSQLSessionInfo &session,
                            const ObTimeZoneInfo *tz_info,
                            MYSQL_PROTOCOL_TYPE protocl_type,
                            char *&src,
                            char *dst,
                            const int64_t dst_len,
                            int64_t &dst_pos) const
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObRecordType::deserialize(ObSchemaGetterGuard &schema_guard,
                              common::ObIAllocator &allocator,
                              sql::ObSQLSessionInfo *session,
                              const ObCharsetType charset,
                              const ObCollationType cs_type,
                              const common::ObTimeZoneInfo *tz_info,
                              const char *&src,
                              char *dst,
                              const int64_t dst_len,
                              int64_t &dst_pos) const
{
  int ret = OB_SUCCESS;
  return ret;
}

int ObRecordType::convert(ObPLResolveCtx &ctx, ObObj *&src, ObObj *&dst) const
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(src));
  CK (OB_NOT_NULL(dst));
  if (OB_FAIL(ret)) {
  } else if (src->is_null() || src->get_ext() == 0) {
    dst->set_null();
  } else {
    if (dst->is_null() || dst->get_ext() == 0) {
      int64_t ptr = 0;
      OZ (newx(ctx.allocator_, &ctx, ptr));
      OX (dst->set_extend(ptr, get_type(), get_init_size(get_member_count())));
    }
    CK (src->is_pl_extend() && ObPLType::PL_RECORD_TYPE == src->get_meta().get_extend_type());
    if (OB_SUCC(ret)) {
      ObPLComposite *src_composite = reinterpret_cast<ObPLComposite*>(src->get_ext());
      ObPLComposite *dst_composite = reinterpret_cast<ObPLComposite*>(dst->get_ext());
      ObPLRecord* src_record = static_cast<ObPLRecord*>(src_composite);
      ObPLRecord* dst_record = static_cast<ObPLRecord*>(dst_composite);
      CK (OB_NOT_NULL(src_composite) && src_composite->is_record());
      CK (OB_NOT_NULL(dst_composite) && dst_composite->is_record());
      CK (OB_NOT_NULL(src_record));
      CK (OB_NOT_NULL(dst_record));
      CK (OB_NOT_NULL(dst_record->get_allocator()));
      if (OB_SUCC(ret)) {
        ObPLResolveCtx resolve_ctx(*dst_record->get_allocator(),
                                    ctx.session_info_,
                                    ctx.schema_guard_,
                                    ctx.package_guard_,
                                    ctx.sql_proxy_,
                                    false);
        for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
          const ObPLDataType *type = get_record_member_type(i);
          ObObj* src_obj = NULL;
          ObObj *dst_obj = NULL;
          OZ (src_record->get_element(i, src_obj));
          OZ (dst_record->get_element(i, dst_obj));
          CK (OB_NOT_NULL(type));
          OZ (type->convert(resolve_ctx, src_obj, dst_obj));
        }
      }
    }
  }
  return ret;
}


//---------- for ObPLCollection ----------

int ObPLComposite::deep_copy(ObPLComposite &src,
                             ObPLComposite *&dest,
                             ObIAllocator &allocator,
                             const ObPLINS *ns,
                             sql::ObSQLSessionInfo *session,
                             bool need_new_allocator,
                             bool ignore_del_element)
{
  int ret = OB_SUCCESS;


  switch (src.get_type()) {
  case PL_RECORD_TYPE: {
    ObPLRecord *composite = NULL;
    bool need_free = false;
    if (NULL == dest) {
      dest = reinterpret_cast<ObPLComposite*>(allocator.alloc(src.get_init_size()));
      composite = static_cast<ObPLRecord*>(dest);
      if (OB_ISNULL(composite)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate composite memory failed", K(ret));
      }
      OX (new(composite)ObPLRecord(src.get_id(), static_cast<ObPLRecord&>(src).get_count()));
      OZ (composite->init_data(allocator, need_new_allocator));
      OX (need_free = true);
      if (OB_FAIL(ret) && OB_NOT_NULL(composite)) {
        allocator.free(composite);
      }
    } else {
      OX (composite = static_cast<ObPLRecord*>(dest));
    }
    if (OB_SUCC(ret)) {
      OZ (composite->deep_copy(static_cast<ObPLRecord&>(src), allocator, ns, session, ignore_del_element));
      if (OB_FAIL(ret) && need_free) {
        ObObj destruct_obj;
        int tmp = OB_SUCCESS;
        destruct_obj.set_extend(reinterpret_cast<int64_t>(composite), composite->get_type());
        tmp = ObUserDefinedType::destruct_objparam(allocator, destruct_obj, session);
        LOG_WARN("fail to deep copy record, release memory", K(ret), K(tmp));
      }
    }
  }
    break;


  default: {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected composite to copy", K(src.get_type()), K(ret));
  }
    break;
  }
  return ret;
}

int ObPLComposite::assign_element(ObObj &src, ObObj &dest, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (src.is_ext()) {
    ObPLComposite *dest_composite = reinterpret_cast<ObPLComposite*>(dest.get_ext());
    ObPLComposite *src_composite = reinterpret_cast<ObPLComposite*>(src.get_ext());
    CK (OB_NOT_NULL(src_composite));
    CK (OB_NOT_NULL(dest_composite));
    OZ (dest_composite->assign(src_composite, &allocator));
  } else {
    OZ (dest.apply(src));
  }
  return ret;
}

int ObPLComposite::copy_element(const ObObj &src,
                                ObObj &dest,
                                ObIAllocator &allocator,
                                const ObPLINS *ns,
                                sql::ObSQLSessionInfo *session,
                                const ObDataType *dest_type,
                                bool need_new_allocator,
                                bool ignore_del_element)
{
  int ret = OB_SUCCESS;
  if (src.is_ext()) {
      ObPLComposite *dest_composite = reinterpret_cast<ObPLComposite*>(dest.get_ext());
      ObPLComposite *src_composite = reinterpret_cast<ObPLComposite*>(src.get_ext());
      if (src_composite != dest_composite) {
        CK (OB_NOT_NULL(src_composite));
        OZ (SMART_CALL(ObPLComposite::deep_copy(*src_composite,
                                    dest_composite,
                                    allocator,
                                    ns,
                                    session,
                                    need_new_allocator,
                                    ignore_del_element)));
        CK (OB_NOT_NULL(dest_composite));
        OX (dest.set_extend(reinterpret_cast<int64_t>(dest_composite),
                            src.get_meta().get_extend_type(),
                            src.get_val_len()));
      }
  } else if (NULL != dest_type && NULL != session && !src.is_null()) {
    ObArenaAllocator tmp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE);
    ObRawExprResType result_type;
    ObObjParam result;
    ObObjParam src_tmp;
    CK (OB_NOT_NULL(dest_type));
    OX (result_type.set_meta(dest_type->get_meta_type()));
    OX (result_type.set_accuracy(dest_type->get_accuracy()));
    OX (src_tmp = src);
    OZ (ObSPIService::spi_convert(*session, tmp_allocator, src_tmp, result_type, result));
    OZ (ObUserDefinedType::destruct_objparam(allocator, dest));
    OZ (deep_copy_obj(allocator, result, dest));
  } else {
    if (src.is_null() && 0 != src.get_unknown()) {
      LOG_INFO("here maybe a bug", K(src), K(&src), K(src.get_unknown()));
    }
    OZ (ObUserDefinedType::destruct_objparam(allocator, dest));
    OZ (deep_copy_obj(allocator, src, dest));
  }
  return ret;
}

int ObPLComposite::assign(ObPLComposite *src, ObIAllocator *allocator)
{
  int64_t size = OB_INVALID_SIZE;
  switch (get_type()) {
  case PL_RECORD_TYPE: {
    size = static_cast<ObPLRecord*>(this)->assign(static_cast<ObPLRecord*>(src), allocator);
  }
    break;
  default: {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected composite to get init size", K(get_type()));
  }
  }
  return size;
}

/*
 * To preserve the expected memory layout between ObPLComposite and its derived
 * classes, this function cannot be virtual.
 * */
int64_t ObPLComposite::get_init_size() const
{
  int64_t size = OB_INVALID_SIZE;
  switch (get_type()) {
  case PL_RECORD_TYPE: {
    size = static_cast<const ObPLRecord*>(this)->get_init_size();
  }
    break;


  default: {
    LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected composite to get init size", K(get_type()));
  }
  }
  return size;
}

int64_t ObPLComposite::get_serialize_size() const
{
  int64_t size = 0;
  size += serialization::encoded_length(type_);
  size += serialization::encoded_length(id_);
  size += serialization::encoded_length(is_null_);
  return size;
}

int ObPLComposite::serialize(char *buf, int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  OZ (serialization::encode(buf, len, pos, type_));
  OZ (serialization::encode(buf, len, pos, id_));
  OZ (serialization::encode(buf, len, pos, is_null_));
  return ret;
}

int ObPLComposite::deserialize(const char* buf, const int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  OZ (serialization::decode(buf, len, pos,type_));
  OZ (serialization::decode(buf, len, pos, id_));
  OZ (serialization::decode(buf, len, pos, is_null_));
  return ret;
}

void ObPLComposite::print() const
{
  switch (get_type()) {
    case PL_RECORD_TYPE: {
      static_cast<const ObPLRecord*>(this)->print();
    }
      break;
    default: {
      LOG_WARN_RET(OB_ERR_UNEXPECTED, "unexpected composite to print", K(get_type()));
    }
    }
}

bool ObPLComposite::obj_is_null(ObObj* obj) {
  int ret = OB_SUCCESS;
  bool is_null = true;
  if (OB_ISNULL(obj)) {
  } else if (obj->is_null()) {
  } else if (obj->is_ext()) {
    if (0 == obj->get_ext()) {
      is_null = true;
    } else if (PL_RECORD_TYPE == obj->get_meta().get_extend_type()) {
      ObPLRecord *record = reinterpret_cast<ObPLRecord*>(obj->get_ext());
      is_null = (record->is_null() || !record->is_inited()) ? true : false;
    } else if (PL_VARRAY_TYPE == obj->get_meta().get_extend_type()
                || PL_NESTED_TABLE_TYPE == obj->get_meta().get_extend_type()
                || PL_ASSOCIATIVE_ARRAY_TYPE == obj->get_meta().get_extend_type()) {
      ObPLCollection *coll = reinterpret_cast<ObPLCollection*>(obj->get_ext());
      is_null = (coll->is_null() || !coll->is_inited()) ? true : false;
    } else {
      is_null = false;
    }
  } else {
    is_null = false;
  }
  return is_null;
}

int ObPLRecord::init_data(common::ObIAllocator &allocator, bool need_new_allocator)
{
  int ret = OB_SUCCESS;
  if (OB_INVALID_COUNT == count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("must construct obplrecord before init data", K(ret));
  } else if (OB_NOT_NULL(data_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cannot init record data twice", K(ret));
  } else {
    ObPLAllocator1 *pl_allocator = static_cast<ObPLAllocator1*>(allocator.alloc(sizeof(ObPLAllocator1)));
    if (OB_ISNULL(pl_allocator)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to alloc memory for record allocator", K(ret));
    } else {
      pl_allocator = new(pl_allocator)ObPLAllocator1(PL_MOD_IDX::OB_PL_RECORD, &allocator);
      OZ (pl_allocator->init(need_new_allocator ? nullptr : &allocator));
      OX (set_allocator(pl_allocator));
    }
    if (OB_SUCC(ret)) {
      ObObj* data = reinterpret_cast<ObObj*>(get_allocator()->alloc(sizeof(ObObj) * count_));
      if (OB_ISNULL(data)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to alloc memory for record data", K(ret));
      } else {
        for (int64_t i = 0; i < count_; ++i) {
          new (data + i) ObObj();
        }
        set_data(data);
      }
    }
    if (OB_FAIL(ret) && OB_NOT_NULL(pl_allocator)) {
      pl_allocator->~ObPLAllocator1();
      allocator.free(pl_allocator);
      set_allocator(nullptr);
    }
  }
  return ret;
}



int ObPLRecord::get_element(int64_t i, ObObj &obj) const
{
  int ret = OB_SUCCESS;
  CK (i >= 0 && i < get_count());
  CK (OB_NOT_NULL(data_));
  OX (obj = data_[i]);
  return ret;
}

int ObPLRecord::get_element(int64_t i, ObObj *&obj)
{
  int ret = OB_SUCCESS;
  CK (i >= 0 && i < get_count());
  CK (OB_NOT_NULL(data_));
  OX (obj = &data_[i]);
  return ret;
}

int ObPLRecord::assign(ObPLRecord *src, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(src));
  if (OB_SUCC(ret)) {
    set_type(src->get_type());
    set_id(src->get_id());
    set_is_null(src->is_null());
    set_count(src->get_count());
    MEMCPY(this->get_not_null(), src->get_not_null(), src->get_init_size() - ObRecordType::get_notnull_offset());
    ObObj src_element;
    ObObj *dest_element = NULL;
    CK (OB_NOT_NULL(get_allocator()));
    for (int64_t i = 0; OB_SUCC(ret) && i < get_count(); ++i) {
      OZ (src->get_element(i, src_element));
      OZ (get_element(i, dest_element));
      OZ (ObPLComposite::assign_element(src_element, *dest_element, *get_allocator()));
    }
  }
  return ret;
}

int ObPLRecord::deep_copy(ObPLRecord &src,
                          ObIAllocator &allocator,
                          const ObPLINS *ns,
                          sql::ObSQLSessionInfo *session,
                          bool ignore_del_element)
{
  int ret = OB_SUCCESS;

  if (!is_inited()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected error", K(ret), K(count_), KPC(data_));
  }
  OV (get_count() == src.get_count(), OB_ERR_WRONG_TYPE_FOR_VAR, K(get_count()), K(src.get_count()));
  CK (OB_NOT_NULL(get_allocator()));
  if (OB_SUCC(ret)) {
    if (get_id() == src.get_id()) {
      set_type(src.get_type());
      set_is_null(src.is_null());
      MEMCPY(this->get_not_null(), src.get_not_null(), src.get_init_size() - ObRecordType::get_notnull_offset());
    }
    const ObUserDefinedType *user_type = NULL;
    const ObRecordType *record_type = NULL;
    if (NULL != ns) {
      OZ (ns->get_user_type(get_id(), user_type, NULL));
      OV (OB_NOT_NULL(user_type), OB_ERR_UNEXPECTED, K(get_id()), K(src.get_id()));
      CK (user_type->is_record_type());
      OX (record_type = static_cast<const ObRecordType*>(user_type));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < get_count(); ++i) {
      ObObj src_element;
      ObObj *dest_element = NULL;
      const ObPLDataType *elem_type = NULL;
      OZ (src.get_element(i, src_element));
      OZ (get_element(i, dest_element));
      if (NULL != record_type) {
        CK (OB_NOT_NULL(elem_type = record_type->get_record_member_type(i)));
      }
      OZ (ObPLComposite::copy_element(src_element,
                                      *dest_element,
                                      *get_allocator(),
                                      ns,
                                      session,
                                      NULL == elem_type ? NULL : elem_type->get_data_type(),
                                      false, /*need_new_allocator*/
                                      ignore_del_element));
    }
  }
  return ret;
}

int ObPLRecord::set_data(const ObIArray<ObObj> &row)
{
  int ret = OB_SUCCESS;
  CK (get_count() == row.count());
  CK (OB_NOT_NULL(data_));
  CK (OB_NOT_NULL(allocator_));
  for (int64_t i = 0; OB_SUCC(ret) && i < row.count(); ++i) {
    ObObj &cur_obj = data_[i];
    if (row.at(i).is_pl_extend()) {
      OZ (ObUserDefinedType::destruct_objparam(*allocator_, cur_obj, nullptr));
      OZ (ObUserDefinedType::deep_copy_obj(*allocator_, row.at(i), cur_obj));
    } else {
      void * ptr = cur_obj.get_deep_copy_obj_ptr();
      if (nullptr != ptr) {
        allocator_->free(ptr);
      }
      OZ (deep_copy_obj(*allocator_, row.at(i), cur_obj));
    }
  }
  return ret;
}

void ObPLRecord::print() const
{
  int ret = OB_SUCCESS;
  LOG_INFO("ObPLRecord Header", K(this), K(*this), K(count_));
  ObObj obj;
  for (int64_t i= 0; i < get_count(); ++i) {
    OZ (get_element(i, obj));
    if (OB_SUCC(ret)) {
      if (obj.is_pl_extend()) {
        ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(obj.get_ext());
        LOG_INFO("ObPLRecord Data", K(i), K(get_count()), K(*composite));
        OX (composite->print());
      } else if (obj.is_varchar_or_char() && obj.get_data_length() > 100) {
        LOG_INFO("ObPLRecord Data", K(i), K(get_count()), K("xxx...xxx"));
      } else {
        LOG_INFO("ObPLRecord Data", K(i), K(get_count()), K(obj));
      }
    }
  }
}





int ObPLCollection::init_allocator(common::ObIAllocator &allocator, bool need_new_allocator)
{
  int ret = OB_SUCCESS;

  ObPLAllocator1 *collection_allocator = nullptr;
  CK (OB_ISNULL(get_allocator()));
  collection_allocator = static_cast<ObPLAllocator1*>(allocator.alloc(sizeof(ObPLAllocator1)));
  if (OB_ISNULL(collection_allocator)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("get a invalud obj", K(ret), K(collection_allocator));
  } else {
    collection_allocator = new(collection_allocator)ObPLAllocator1(PL_MOD_IDX::OB_PL_COLLECTION, &allocator);
    OZ (collection_allocator->init(need_new_allocator ? nullptr : &allocator));
    if (OB_SUCC(ret)) {
      set_allocator(collection_allocator);
    } else {
      allocator.free(collection_allocator);
    }
  }

  return ret;
}

/*
 * We agree on a principle:
 * 1、All ObObj arrays in the data field of a Collection (including memory in the sort and key fields) must be allocated by the Collection's own allocator, and not by any other allocator;
 * 2、If the data field contains basic data types, the memory should also be allocated by the Collection's own allocator;
 * 3、If the data field is a record, the memory for the record itself should also be allocated by the Collection's own allocator; the memory for basic data types within the record should also be allocated by the Collection's own allocator;
 * 4、If the data field contains a sub-Collection, the sub-Collection data structure itself should be allocated by the parent Collection's allocator; memory management for the sub-Collection should recursively follow this agreement.
 * */

int ObPLCollection::assign(ObPLCollection *src, ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  ObObj *new_objs = NULL;
  ObObj *old_objs = NULL;
  ObIAllocator *coll_allocator = NULL == allocator_ ? allocator : allocator_;
  CK (OB_NOT_NULL(coll_allocator));
  CK (OB_NOT_NULL(src) && src->is_collection());
  if (OB_SUCC(ret)) {
    void* data = NULL;
    if (src->get_inner_capacity() > 0) {
      data = coll_allocator->alloc(src->get_inner_capacity() * sizeof(ObObj));
      if (OB_ISNULL(data)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failed to allocate memory for collection",
                 K(ret), K(src->get_count()));
      }
      CK (OB_NOT_NULL(new_objs = reinterpret_cast<ObObj*>(data)));
      CK (OB_NOT_NULL(old_objs = reinterpret_cast<ObObj*>(src->get_data())));
      for (int64_t i = 0; OB_SUCC(ret) && i < src->get_count(); ++i) {
        new (&new_objs[i])ObObj();
        OZ (ObPLComposite::assign_element(old_objs[i], new_objs[i], *coll_allocator));
      }
      for (int64_t i = src->get_count(); OB_SUCC(ret) && i < src->get_inner_capacity(); ++i) {
        new (&new_objs[i])ObObj();
      }
    }
    if (OB_SUCC(ret)) {
      set_allocator(coll_allocator);
      set_type(src->get_type());
      set_id(src->get_id());
      set_is_null(src->is_null());
      set_element_desc(src->get_element_desc());
      set_count(src->get_count());
      set_first(src->get_pure_first());
      set_last(src->get_pure_last());
      set_data(new_objs, src->get_inner_capacity());
    }
  }
  return ret;
}


int ObPLCollection::is_elem_deleted(int64_t index, bool &is_del) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(0 > index || index > get_count() - 1)) {
    ret = OB_ARRAY_OUT_OF_RANGE;
    LOG_WARN("array index out of range.", K(index), K(get_count()));
  } else if (OB_ISNULL(get_data())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("collection is uninited", K(ret));
  } else {
    ObObj *obj = const_cast<ObObj *>(static_cast<const ObObj *>(get_data()));
    is_del = obj[index].is_invalid_type();
  }

  return ret;
}



int64_t ObPLCollection::get_actual_count()
{
  int64_t count = get_count();
  int64_t cnt = 0;
  ObObj *objs = static_cast<ObObj*>(get_data());
  for (int64_t i = 0; i < count; ++i) {
    if (objs[i].is_invalid_type()) {
      cnt++;
    } else {
      LOG_DEBUG("array out of range.", K(i), K(cnt), K(count));
    }
  }
  return count - cnt;
}

int ObPLCollection::update_first_impl()
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_ERR_COLLECION_NULL;
    LOG_WARN("pl collection is not inited", K(ret));
  } else if (0 > count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("collection is empty", K(count_), K(ret));
  } else {
    #define FIND_FIRST(start, end) \
    do {\
    for (int64_t i = start; OB_SUCC(ret) && i <= end; ++i) { \
      if (OB_FAIL(is_elem_deleted(i, is_deleted))) { \
        LOG_WARN("unexpected first index", K(ret));\
      } else if (!is_deleted) {\
        OX (set_first(i + 1));\
        break;\
      }\
    } }while(0)

    bool is_deleted = false;
    // When there is an assignment, both first and last will be set to that value, so a full traversal from the beginning is needed.
    // Why is this needed, it is to optimize performance, for example, now first is 4, at this time the assignment of 2 is being assigned.
    // So it needs to traverse from the beginning. However, when delete is performed, this operation is not done, so we only need to check if the one corresponding to first is valid.
    if (OB_INVALID_INDEX == first_) {
      FIND_FIRST(0, count_ - 1);
    } else {
      FIND_FIRST(first_ - 1, count_ - 1);
    }
  }
  return ret;
}

int ObPLCollection::update_last_impl()
{
  int ret = OB_SUCCESS;
  if (!is_inited()) {
    ret = OB_ERR_COLLECION_NULL;
    LOG_WARN("pl collection is not inited", K(ret));
  } else if (0 > count_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("collection is empty", K(count_), K(ret));
  } else {

#define FIND_LAST(start, end)                                     \
  do {                                                            \
    for (int64_t i = start; OB_SUCC(ret) && i >= end; --i) {      \
      if (OB_FAIL(is_elem_deleted(i, is_deleted))) {              \
        LOG_WARN("unexpected last index", K(ret));                \
      } else if (!is_deleted) {                                   \
        OX (set_last(i + 1));                                     \
        break;                                                    \
      }                                                           \
    }                                                             \
  } while(0);

    bool is_deleted = true;
    if (OB_INVALID_INDEX == last_) {
      FIND_LAST(count_ - 1, 0);
    } else {
      FIND_LAST(last_ - 1, 0);
    }

#undef FIND_LAST
  }
  return ret;
}

int64_t ObPLCollection::get_first()
{
  int ret = OB_SUCCESS;
  int64_t first = first_;
  if (OB_FAIL(update_first_impl())) {
    first = OB_INVALID_INDEX;
    LOG_WARN("update collection first failed.", K(ret), K(first), K(first_));
  } else {
    first = first_;
  }
  return first;
}

int64_t ObPLCollection::get_last()
{
  int ret = OB_SUCCESS;
  int64_t last = last_;
  if (OB_FAIL(update_last_impl())) {
    last = OB_INVALID_INDEX;
    LOG_WARN("update collection last failed.", K(ret), K(last), K(last_));
  } else {
    last = last_;
  }
  return last;
}



void ObPLCollection::print() const
{
  int ret = OB_SUCCESS;
  const int64_t *sort_array = nullptr;
  const ObObj *key_array = nullptr;
  LOG_INFO("ObPLCollection Header", K(this), K(*this));


  for (int64_t i = 0; i < count_; ++i) {
    ObObj &obj = data_[i];
    const ObObj *key = key_array != nullptr ? &(key_array[i]) : nullptr;
    const int64_t sort = sort_array != nullptr ? sort_array[i] : OB_INVALID_INDEX;
    if (obj.is_pl_extend()) {
      ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(obj.get_ext());
      LOG_INFO("ObPLCollection Data", K(i), K(get_count()), K(sort), KPC(key), K(*composite));
      OX (composite->print());
    } else if (obj.is_varchar_or_char() && obj.get_data_length() > 100) {
      LOG_INFO("ObPLCollection Data", K(i), K(get_count()), K(sort), KPC(key), K("xxx...xxx"));
    } else if (obj.is_invalid_type()) {
      LOG_INFO("ObPLCollection Data", K(i), K(get_count()), K(sort), KPC(key), K("deleted element"), K(obj));
    } else {
      LOG_INFO("ObPLCollection Data", K(i), K(get_count()), K(sort), KPC(key), K(obj));
    }
  }
}




/*
 * The data in the Collection has multiple cases that need to be handled separately:
 * 1、Simple type, element_ is not extend, col_cnt_ is 1: directly write in order to Obj
 * 2、Complex type, element_ is extend, col_cnt_ is 1: there are two possible situations:
 *        a、it might be a Record with only one element: need to construct Record space
 *        b、it might be a Collection, directly write to Obj
 * 3、Complex type, element_ is extend, col_cnt_ greater than 1: indicates it's a Record: need to construct Record space
 * */
int ObPLCollection::set_row(const ObIArray<ObObj> &row, int64_t idx, bool deep_copy)
{
  int ret = OB_SUCCESS;
  CK (!row.empty());
  OV (idx >= 0 && idx < get_count(), OB_ERR_UNEXPECTED, idx, get_count());
  OV (element_.get_field_count() == row.count(), OB_ERR_UNEXPECTED, element_, row);
  if (OB_FAIL(ret)) {
  } else if (deep_copy) {
    //TODO: @ryan.ly
  } else {
    ObObj &data_obj = data_[idx];
    if (element_.is_composite_type()) {
      if (data_obj.is_ext()) { // already extend, which means the space has already allocated memory, we can directly write to the memory
        CK (0 != data_obj.get_ext());
        if (OB_SUCC(ret)) {
          ObPLComposite *composite = reinterpret_cast<ObPLComposite*>(data_obj.get_ext());
          if (composite->is_record()) {
            ObPLRecord *record = static_cast<ObPLRecord*>(composite);
            OZ (record->set_data(row));
          } else if (composite->is_collection()) {
            CK (1 == row.count() && row.at(0).is_ext());
            OZ (ObUserDefinedType::destruct_objparam(*allocator_, data_obj, nullptr));
            OZ (ObUserDefinedType::deep_copy_obj(*allocator_, row.at(0), data_obj));
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Unexpected composite in array", K(*composite), K(ret));
          }
        }
      } else if (data_obj.is_null()) { // space has not been allocated, need to allocate
        if (element_.is_record_type()) {
          ObPLRecord *new_record = reinterpret_cast<ObPLRecord*>(
              allocator_->alloc(ObRecordType::get_init_size(element_.get_field_count())));
          if (OB_ISNULL(new_record)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("allocate composite memory failed", K(ret));
          }
          OX (new (new_record)ObPLRecord(element_.get_udt_id(), element_.get_field_count()));
          OZ (new_record->init_data(*allocator_, false));
          if (OB_FAIL(ret)) {
            if (OB_NOT_NULL(new_record)) {
              allocator_->free(new_record);
            }
          } else {
            OX (new_record->set_data(row));
            OX (data_obj.set_extend(reinterpret_cast<int64_t>(new_record),
                                    PL_RECORD_TYPE,
                                    ObRecordType::get_init_size(element_.get_field_count())));
          }
        } else {
          CK (1 == row.count());
          OZ (deep_copy_obj(*allocator_, row.at(0), data_obj));
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected data in array", K(data_obj), K(element_), K(ret));
      }
    } else {
      CK (1 == row.count());
      OZ (deep_copy_obj(*allocator_, row.at(0), data_obj));
    }
  }
  return ret;
}








}  // namespace pl
}  // namespace oceanbase
