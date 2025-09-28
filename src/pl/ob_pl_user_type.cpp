/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX PL

#include "ob_pl_user_type.h"
#include "observer/mysql/obsm_utils.h"
#include "pl/ob_pl_code_generator.h"
#include "pl/ob_pl_package.h"
#include "observer/mysql/ob_query_driver.h"

namespace oceanbase
{
using namespace common;
using namespace share::schema;
using namespace jit;
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

int ObUserDefinedType::generate_assign_with_null(
  ObPLCodeGenerator &generator,
  const ObPLINS &ns, jit::ObLLVMValue &allocator, jit::ObLLVMValue &dest) const
{
  UNUSEDx(generator, ns, allocator, dest); return OB_SUCCESS;
}

int ObUserDefinedType::generate_default_value(
  ObPLCodeGenerator &generator,
  const ObPLINS &ns, const pl::ObPLStmt *stmt, jit::ObLLVMValue &value, jit::ObLLVMValue &allocator, bool is_top_level) const
{
  UNUSEDx(generator, ns, stmt, value, allocator); return OB_SUCCESS;
}

int ObUserDefinedType::generate_copy(
  ObPLCodeGenerator &generator, const ObPLBlockNS &ns,
  jit::ObLLVMValue &allocator, jit::ObLLVMValue &src, jit::ObLLVMValue &dest,
  uint64_t location, bool in_notfound, bool in_warning, uint64_t package_id) const
{
  UNUSEDx(generator, ns, allocator, src, dest, in_notfound, in_warning, package_id);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
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

int ObUserDefinedType::get_serialize_size(
    const ObPLResolveCtx &resolve_ctx, char *&src, int64_t &size) const
{
  UNUSEDx(resolve_ctx, src, size);
  char err_msg[number::ObNumber::MAX_PRINTABLE_SIZE] = {0};
  (void)snprintf(err_msg, sizeof(err_msg), "%s serialize", get_name().ptr());
  LOG_USER_ERROR(OB_NOT_SUPPORTED, err_msg);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::serialize(
    const ObPLResolveCtx &resolve_ctx,
    char *&src, char* dst, int64_t dst_len, int64_t &dst_pos) const
{
  UNUSEDx(resolve_ctx, src, dst, dst_len, dst_pos);
  LOG_WARN_RET(OB_NOT_SUPPORTED, "Call virtual func of ObUserDefinedType! May forgot implement in SubClass", K(this));
  return OB_NOT_SUPPORTED;
}

int ObUserDefinedType::deserialize(
    const ObPLResolveCtx &resolve_ctx,
    common::ObIAllocator &allocator,
    const char* src, const int64_t src_len, int64_t &src_pos, char *&dst) const
{
  UNUSEDx(resolve_ctx, allocator, src, src_len, src_pos, dst);
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


int ObUserDefinedType::generate_new(ObPLCodeGenerator &generator,
                                          const ObPLINS &ns,
                                          jit::ObLLVMValue &value, //返回值是一个int64_t，代表extend的值
                                          jit::ObLLVMValue &allocator,
                                          bool is_top_level,
                                          const pl::ObPLStmt *s) const
{
  int ret = OB_SUCCESS;
  ObLLVMValue composite_value;
  ObLLVMType ir_type;
  ObLLVMType ir_pointer_type;

  OZ (generator.get_llvm_type(*this, ir_type));
  OZ (ir_type.get_pointer_to(ir_pointer_type));
  OZ (generator.get_helper().create_int_to_ptr(ObString("ptr_to_user_type"), value, ir_pointer_type,
                                             composite_value));
  OX (composite_value.set_t(ir_type));
  OZ (generate_construct(generator, ns, composite_value, allocator, is_top_level, s));
  return ret;
}

int ObUserDefinedType::generate_construct(ObPLCodeGenerator &generator,
                                          const ObPLINS &ns,
                                          jit::ObLLVMValue &value,
                                          jit::ObLLVMValue &allocator,
                                          bool is_top_level,
                                          const pl::ObPLStmt *stmt) const
{
  int ret = OB_SUCCESS;
  UNUSED(ns);
  UNUSED(stmt);
  jit::ObLLVMType ir_type;
  jit::ObLLVMValue const_value;
  OZ (generator.get_llvm_type(*this, ir_type));
  OZ (jit::ObLLVMHelper::get_null_const(ir_type, const_value));
  OZ (generator.get_helper().create_store(const_value, value));
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
      OZ (ObRefCursorType::deep_copy_cursor(allocator, src, dst));
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

// keep_composite_attr = true, 保留其allocator属性，对于record而言，保留data域
// 否则, 所有内存都清理
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
    case PL_REF_CURSOR_TYPE: {
      // do nothing
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
          //只定义过而没有用过的Record的allocator为空，这是正常的，跳过即可
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
  OZ (serialization::encode(buf, len, pos, GET_MIN_CLUSTER_VERSION()));
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
  OX (size += serialization::encoded_length(GET_MIN_CLUSTER_VERSION()));
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

int ObUserDefinedType::generate_init_composite(ObPLCodeGenerator &generator,
                                                const ObPLINS &ns,
                                                jit::ObLLVMValue &value,
                                                const pl::ObPLStmt *stmt,
                                                jit::ObLLVMValue &allocator,
                                                bool is_record_type,
                                                bool is_top_level)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObLLVMValue, 3> args;
  ObLLVMValue ret_err;
  ObLLVMValue addr;
  ObLLVMType int_type;
  ObLLVMValue int_value, is_record, is_top;
  OZ (generator.get_helper().get_llvm_type(ObIntType, int_type));
  OZ (generator.get_helper().create_ptr_to_int(ObString("composite_to_int64"),
                                               value,
                                               int_type,
                                               int_value));
  OZ (args.push_back(allocator));
  OZ (args.push_back(int_value));
  OZ (generator.get_helper().get_int8(is_record_type, is_record));
  OZ (args.push_back(is_record));
  OZ (generator.get_helper().get_int8(is_top_level, is_top));
  OZ (args.push_back(is_top));
  OZ (generator.get_helper().create_call(ObString("spi_init_composite"),
                                         generator.get_spi_service().spi_init_composite_,
                                         args,
                                         ret_err));
  OZ (generator.check_success(ret_err,
                              stmt->get_stmt_id(),
                              stmt->get_block()->in_notfound(),
                              stmt->get_block()->in_warning()));
  return ret;
}


//---------- for ObRefCursorType ----------

int ObRefCursorType::deep_copy(common::ObIAllocator &alloc, const ObRefCursorType &other)
{
  int ret = OB_SUCCESS;
  OZ (ObUserDefinedType::deep_copy(alloc, other));
  OX (return_type_id_ = other.return_type_id_);
  return ret;
}

int ObRefCursorType::generate_construct(ObPLCodeGenerator &generator,
                                        const ObPLINS &ns,
                                        jit::ObLLVMValue &value,
                                        jit::ObLLVMValue &allocator,
                                        bool is_top_level,
                                        const pl::ObPLStmt *stmt) const
{
  UNUSEDx(generator, ns, value, stmt);
  return OB_NOT_SUPPORTED;
}

int ObRefCursorType::generate_new(ObPLCodeGenerator &generator,
                                              const ObPLINS &ns,
                                              jit::ObLLVMValue &value,
                                              jit::ObLLVMValue &allocator,
                                              bool is_top_level,
                                              const pl::ObPLStmt *s) const
{
  UNUSED(generator);
  UNUSED(ns);
  UNUSED(value);
  UNUSED(s);
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRefCursorType::newx(common::ObIAllocator &allocator, const ObPLINS *ns, int64_t &ptr) const
{
  int ret = OB_NOT_SUPPORTED;
  UNUSEDx(allocator, ns, ptr);
  return ret;
}

int ObRefCursorType::get_size(ObPLTypeSize type, int64_t &size) const
{
  UNUSEDx(type, size);
  size = sizeof(ObPLCursorInfo) + 8;
  return OB_SUCCESS;
}

int ObRefCursorType::init_obj(ObSchemaGetterGuard &schema_guard,
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

int ObRefCursorType::init_session_var(const ObPLResolveCtx &resolve_ctx,
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

int ObRefCursorType::deep_copy_cursor(common::ObIAllocator &allocator,
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
      if (common::ObCharset::case_compat_mode_equal(
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
      if (common::ObCharset::case_compat_mode_equal(
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
      if (common::ObCharset::case_compat_mode_equal(
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
    if (common::ObCharset::case_compat_mode_equal(
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
  return ObRecordType::get_notnull_offset() + 8 * ((count - 1) / 8 + 1); //notnull是bool，需要对齐
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

int ObRecordType::generate_assign_with_null(ObPLCodeGenerator &generator,
                                            const ObPLINS &ns,
                                            jit::ObLLVMValue &allocator,
                                            jit::ObLLVMValue &dest) const
{
  /*
   * ORACLE 12.1 Document, Page 196:
   * Assigning the value NULL to a record variable assigns the value NULL to each of its fields.
   */
  int ret = OB_SUCCESS;
  ObLLVMValue isnull_ptr;
  ObLLVMValue dest_elem;
  ObObj null_obj;
  null_obj.set_null();
  const ObPLDataType *member_type = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < get_record_member_count(); ++i) {
    dest_elem.reset();
    if (OB_FAIL(generator.extract_element_ptr_from_record(dest,
                                                          get_record_member_count(),
                                                          i,
                                                          dest_elem))) {
      LOG_WARN("failed to create gep", K(ret));
    } else if (OB_ISNULL(member_type = get_record_member_type(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get member type", K(ret));
    } else if (member_type->is_composite_type()) {
      ObLLVMValue extend;
      OZ (generator.extract_extend_from_obj(dest_elem, *member_type, extend));
      OZ (member_type->generate_assign_with_null(generator, ns, allocator, extend));
    } else {
      ObSEArray<jit::ObLLVMValue, 2> args;
      ObLLVMType int_type;
      ObLLVMValue int_value, is_record, member_idx;
      if (OB_FAIL(generator.get_helper().get_llvm_type(ObIntType, int_type))) {
        LOG_WARN("failed to get_llvm_type", K(ret));
      } else if (OB_FAIL(generator.get_helper().create_ptr_to_int(ObString("cast_ptr_to_int64"), dest,
                                                                  int_type, int_value))) {
        LOG_WARN("failed to create ptr to int", K(ret));
      } else if (OB_FAIL(args.push_back(int_value))) {
        LOG_WARN("push_back error", K(ret));
      } else if (OB_FAIL(generator.get_helper().get_int8(true, is_record))) {
        LOG_WARN("fail to get int8", K(ret));
      } else if (OB_FAIL(args.push_back(is_record))) {
        LOG_WARN("push_back error", K(ret));
      } else if (OB_FAIL(generator.get_helper().get_int32(i, member_idx))) {
        LOG_WARN("fail to get int8", K(ret));
      } else if (OB_FAIL(args.push_back(member_idx))) {
        LOG_WARN("push_back error", K(ret));
      } else {
        jit::ObLLVMValue ret_err;
        if (OB_FAIL(generator.get_helper().create_call(ObString("spi_reset_composite"),
            generator.get_spi_service().spi_reset_composite_, args, ret_err))) {
          LOG_WARN("failed to create call", K(ret));
        } else if (OB_FAIL(generator.check_success(ret_err))) {
          LOG_WARN("failed to check success", K(ret));
        } else if (OB_FAIL(generator.store_obj(null_obj, dest_elem))) {
          LOG_WARN("failed to create store", K(ret));
        }
      }
    }
  }
  OZ (generator.extract_isnull_ptr_from_record(dest, isnull_ptr));
  OZ (generator.get_helper().create_istore(TRUE, isnull_ptr));
  return ret;
}

int ObRecordType::generate_construct(ObPLCodeGenerator &generator,
                                     const ObPLINS &ns,
                                     jit::ObLLVMValue &value,
                                     jit::ObLLVMValue &allocator,
                                     bool is_top_level,
                                     const pl::ObPLStmt *stmt) const
{
  int ret = OB_SUCCESS;
  OZ (SMART_CALL(ObUserDefinedType::generate_construct(generator, ns, value, allocator, is_top_level, stmt)));
  OZ (SMART_CALL(generate_default_value(generator, ns, stmt, value, allocator, is_top_level)));
  return ret;
}

int ObRecordType::generate_new(ObPLCodeGenerator &generator,
                                              const ObPLINS &ns,
                                              jit::ObLLVMValue &value,
                                              jit::ObLLVMValue &allocator,
                                              bool is_top_level,
                                              const pl::ObPLStmt *s) const
{
  int ret = OB_NOT_SUPPORTED;
  ret = ObUserDefinedType::generate_new(generator, ns, value, allocator, is_top_level, s);
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

int ObRecordType::generate_alloc_complex_addr(ObPLCodeGenerator &generator,
                                              int8_t type,
                                              int64_t user_type_id,
                                              int64_t init_size,
                                              jit::ObLLVMValue &value, //返回值是一个int64_t，代表extend的值
                                              jit::ObLLVMValue &allocator,
                                              const pl::ObPLStmt *s)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObLLVMValue, 8> args;
  ObLLVMValue var_idx, init_value;
  ObLLVMValue extend_ptr;
  ObLLVMValue ret_err;
  ObLLVMValue var_type, type_id;
  ObPLCGBufferGuard buffer_guard(generator);

  OZ (buffer_guard.get_int_buffer(extend_ptr));
  OZ (args.push_back(generator.get_vars().at(generator.CTX_IDX)));
  OZ (generator.get_helper().get_int8(type, var_type));
  OZ (args.push_back(var_type));
  OZ (generator.get_helper().get_int64(user_type_id, type_id));
  OZ (args.push_back(type_id));
  OZ (generator.get_helper().get_int64(OB_INVALID_INDEX, var_idx));
  OZ (args.push_back(var_idx));
  OZ (generator.get_helper().get_int32(init_size, init_value));
  OZ (args.push_back(init_value));
  OZ (args.push_back(extend_ptr));
  OZ (args.push_back(allocator));
  OZ (generator.get_helper().create_call(ObString("spi_alloc_complex_var"),
                                         generator.get_spi_service().spi_alloc_complex_var_,
                                         args,
                                         ret_err));
  OZ (generator.check_success(ret_err,
                              s->get_stmt_id(),
                              s->get_block()->in_notfound(),
                              s->get_block()->in_warning()));

  OZ (generator.get_helper().create_load("load_extend_ptr", extend_ptr, value));
  return ret;
}

int ObRecordType::generate_default_value(ObPLCodeGenerator &generator,
                                         const ObPLINS &ns,
                                         const ObPLStmt *stmt,
                                         jit::ObLLVMValue &value,
                                         jit::ObLLVMValue &allocator,
                                         bool is_top_level) const
{
  int ret = OB_SUCCESS;
  ObLLVMValue type_value;
  ObLLVMValue type_ptr;
  ObLLVMValue id_value;
  ObLLVMValue id_ptr;
  ObLLVMValue isnull_value;
  ObLLVMValue isnull_ptr;
  ObLLVMValue count_value;
  ObLLVMValue count_ptr;
  ObLLVMValue notnull_value;
  ObLLVMValue notnull_ptr;
  ObLLVMValue meta_value;
  ObLLVMValue meta_ptr;
  ObDataType meta;
  const ObRecordMember *member = NULL;
  int64_t result_idx = OB_INVALID_INDEX;
  ObLLVMValue obobj_res;
  ObLLVMValue ptr_elem;
  ObObj null_obj;

  //设置composite和count
  OZ (generator.get_helper().get_int32(type_, type_value));
  OZ (generator.extract_type_ptr_from_record(value, type_ptr));
  OZ (generator.get_helper().create_store(type_value, type_ptr));
  OZ (generator.get_helper().get_int64(user_type_id_, id_value));
  OZ (generator.extract_id_ptr_from_record(value, id_ptr));
  OZ (generator.get_helper().create_store(id_value, id_ptr));
  if (is_object_type()) {
    OZ (generator.get_helper().get_int8(TRUE, isnull_value));
  } else {
    OZ (generator.get_helper().get_int8(FALSE, isnull_value));
  }
  OZ (generator.extract_isnull_ptr_from_record(value, isnull_ptr));
  OZ (generator.get_helper().create_store(isnull_value, isnull_ptr));
  OZ (generator.get_helper().get_int32( get_record_member_count(), count_value));
  OZ (generator.extract_count_ptr_from_record(value, count_ptr));
  OZ (generator.get_helper().create_store(count_value, count_ptr));
  OZ (ObUserDefinedType::generate_init_composite(generator, ns, value, stmt, allocator, true, is_top_level));
  OZ (generator.generate_debug("generate_default_value", value));
  //设置meta和数据
  null_obj.set_null();
  CK (OB_NOT_NULL(stmt));
  for (int64_t i = 0; OB_SUCC(ret) && i < get_record_member_count(); ++i) {
    ObLLVMValue result;
    ObPLCGBufferGuard buffer_guard(generator);

    member = get_record_member(i);
    CK (OB_NOT_NULL(member));

    //设置notnull和meta
    if (OB_SUCC(ret)) {
      meta.reset();
      if (NULL == member->member_type_.get_data_type()) {
        meta.set_obj_type(ObExtendType);
      } else {
        meta = *member->member_type_.get_data_type();
      }
      OZ (generator.get_helper().get_int8(false, notnull_value));
      OZ (generator.extract_notnull_ptr_from_record(value, i, notnull_ptr));
      OZ (generator.get_helper().create_store(notnull_value, notnull_ptr));
      OZ (generator.extract_meta_ptr_from_record(value, get_record_member_count(), i, meta_ptr));
      OZ (generator.store_data_type(meta, meta_ptr));
    }

    OZ (buffer_guard.get_objparam_buffer(result));

    //设置数据
    if (OB_SUCC(ret)) {
      if (OB_INVALID_INDEX != member->get_default()) {
        if (OB_NOT_NULL(member->get_default_expr())) {
          OZ (generator.generate_expr(member->get_default(), *stmt, result_idx, result));
        } else {
          OV (is_package_type(), OB_ERR_UNEXPECTED, KPC(this));
          OZ (generator.generate_spi_package_calc(extract_package_id(get_user_type_id()),
                                                  member->get_default(),
                                                  *stmt,
                                                  result));
        }
        OZ (generator.extract_obobj_from_objparam(result, obobj_res));
      }
      if (OB_SUCC(ret)) {
        ptr_elem.reset();
        OZ (generator.extract_element_ptr_from_record(value,
                                                      get_record_member_count(),
                                                      i,
                                                      ptr_elem));
        OZ (generator.generate_debug("generate_extract_value", ptr_elem));
        if (OB_FAIL(ret)) {
        } else if (member->member_type_.is_obj_type() || OB_INVALID_INDEX != member->get_default()) {
          //不论基础类型还是复杂类型，如果有default，直接把default值存入即可
          if (OB_INVALID_INDEX != member->get_default()) {
            ObLLVMValue record_allocator;
            ObLLVMValue src_datum;
            ObLLVMValue dst_datum;
            OZ (generator.extract_allocator_from_record(value, record_allocator));
            OZ (generator.extract_obobj_ptr_from_objparam(result, src_datum));
            OZ (member->member_type_.generate_copy(generator,
                                                   stmt->get_block()->get_namespace(),
                                                   record_allocator,
                                                   src_datum,
                                                   ptr_elem,
                                                   stmt->get_location(),
                                                   stmt->get_block()->in_notfound(),
                                                   stmt->get_block()->in_warning(),
                                                   OB_INVALID_ID));
            OZ (generator.generate_check_not_null(*stmt,
                                                  member->member_type_.get_not_null(),
                                                  result));
          } else {
            OZ (generator.store_obj(null_obj, ptr_elem));
          }
          if (OB_SUCC(ret) && !member->member_type_.is_obj_type()) { // process complex null value
            ObLLVMBasicBlock null_branch;
            ObLLVMBasicBlock final_branch;
            ObLLVMValue p_type_value;
            ObLLVMValue type_value;
            ObLLVMValue is_null;
            ObLLVMValue record_allocator;
            ObLLVMValue extend_value;
            ObLLVMValue init_value;
            ObLLVMValue composite_value;
            ObLLVMType ir_type;
            ObLLVMType ir_pointer_type;
            int64_t init_size = OB_INVALID_SIZE;
            OZ (generator.get_helper().create_block(ObString("null_branch"), generator.get_func(), null_branch));
            OZ (generator.get_helper().create_block(ObString("final_branch"), generator.get_func(), final_branch));
            OZ (generator.extract_type_ptr_from_objparam(result, p_type_value));
            OZ (generator.get_helper().create_load(ObString("load_type"), p_type_value, type_value));
            OZ (generator.get_helper().create_icmp_eq(type_value, ObNullType, is_null));
            OZ (generator.get_helper().create_cond_br(is_null, null_branch, final_branch));
            // null branch
            OZ (generator.set_current(null_branch));
            OZ (generator.extract_allocator_from_record(value, record_allocator));
            OZ (ns.get_size(PL_TYPE_INIT_SIZE, member->member_type_, init_size));
            OZ (generator.get_helper().get_int32(init_size, init_value));
            OZ (generate_alloc_complex_addr(generator,
                                            member->member_type_.get_type(),
                                            member->member_type_.get_user_type_id(),
                                            init_size,
                                            extend_value,
                                            record_allocator,
                                            stmt));
            OZ (generator.get_helper().get_int8(member->member_type_.get_type(), type_value));
            OZ (generator.generate_set_extend(ptr_elem, type_value, init_value, extend_value));
            OZ (SMART_CALL(member->member_type_.generate_new(generator, ns, extend_value, record_allocator, false, stmt)));
            OZ (generator.generate_null(ObIntType, record_allocator));
            OZ (generator.get_llvm_type(member->member_type_, ir_type));
            OZ (ir_type.get_pointer_to(ir_pointer_type));
            OZ (generator.get_helper().create_int_to_ptr(ObString("cast_extend_to_ptr"), extend_value, ir_pointer_type, composite_value));
            OX (composite_value.set_t(ir_type));
            OZ (member->member_type_.generate_assign_with_null(generator, ns, record_allocator, composite_value));
            OZ (generator.get_helper().create_br(final_branch));
            // final branch
            OZ (generator.set_current(final_branch));
          }
        } else { //复杂类型如果没有default，调用generate_new
          ObLLVMValue extend_value;
          ObLLVMValue type_value;
          ObLLVMValue init_value;
          ObLLVMValue record_allocator;
          int64_t init_size = OB_INVALID_SIZE;
          int64_t size = OB_INVALID_SIZE;
          OZ (generator.extract_allocator_from_record(value, record_allocator));
          OZ (ns.get_size(PL_TYPE_INIT_SIZE, member->member_type_, init_size));
          OZ (generator.get_helper().get_int32(init_size, init_value));
          OZ (generate_alloc_complex_addr(generator,
                                          member->member_type_.get_type(),
                                          member->member_type_.get_user_type_id(),
                                          init_size,
                                          extend_value,
                                          record_allocator,
                                          stmt));
          OZ (generator.get_helper().get_int8(member->member_type_.get_type(), type_value));
          OZ (generator.generate_set_extend(ptr_elem, type_value, init_value, extend_value));
          OZ (SMART_CALL(member->member_type_.generate_new(generator, ns, extend_value, record_allocator, false, stmt)));
        }
      }
    }
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
  ObArenaAllocator tmp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_INIT_SESSION_VAR), OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID());
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

// --------- for session serialize/deserialize interface ---------
int ObRecordType::get_serialize_size(
  const ObPLResolveCtx &resolve_ctx, char *&src, int64_t &size) const
{
  int ret = OB_SUCCESS;
  ObPLRecord *record = reinterpret_cast<ObPLRecord *>(src);
  CK (OB_NOT_NULL(record));
  OV (record->get_count() == record_members_.count(), OB_ERR_WRONG_TYPE_FOR_VAR, KPC(record), K(record_members_));
  OX (size += record->get_serialize_size());
  OX (size += serialization::encoded_length(record->get_count()));

  char *data = reinterpret_cast<char*>(record->get_element());
  for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
    const ObPLDataType *type = get_record_member_type(i);
    CK (OB_NOT_NULL(type));
    OZ (type->get_serialize_size(resolve_ctx, data, size));
  }
  return ret;
}

int ObRecordType::serialize(
  const ObPLResolveCtx &resolve_ctx,
  char *&src, char* dst, int64_t dst_len, int64_t &dst_pos) const
{
  int ret = OB_SUCCESS;
  ObPLRecord *record = reinterpret_cast<ObPLRecord *>(src);
  CK (OB_NOT_NULL(record));
  CK (record->get_count() == record_members_.count());
  OX (record->serialize(dst, dst_len, dst_pos));
  OZ (serialization::encode(dst, dst_len, dst_pos, record->get_count()));

  char *data = reinterpret_cast<char*>(record->get_element());
  CK (OB_NOT_NULL(data));
  for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
    const ObPLDataType *type = get_record_member_type(i);
    CK (OB_NOT_NULL(type));
    OZ (type->serialize(resolve_ctx, data, dst, dst_len, dst_pos));
  }
  return ret;
}

int ObRecordType::deserialize(
  const ObPLResolveCtx &resolve_ctx,
  common::ObIAllocator &allocator,
  const char* src, const int64_t src_len, int64_t &src_pos, char *&dst) const
{
  int ret = OB_SUCCESS;
  ObPLRecord *record = reinterpret_cast<ObPLRecord *>(dst);
  CK (OB_NOT_NULL(record));
  int32_t count = OB_INVALID_COUNT;
  // when record be delete , type will be PL_INVALID_TYPE
  OX (record->deserialize(src, src_len, src_pos));
  if (OB_SUCC(ret) && record->get_type() != PL_INVALID_TYPE) {
    OZ (serialization::decode(src, src_len, src_pos, count));
    CK (count == record_members_.count());
    OX (record->set_count(count));

    dst = reinterpret_cast<char*>(record->get_element());
    CK (OB_NOT_NULL(dst));
    CK (OB_NOT_NULL(record->get_allocator()));
    for (int64_t i = 0; OB_SUCC(ret) && i < record_members_.count(); ++i) {
      const ObPLDataType *type = get_record_member_type(i);
      CK (OB_NOT_NULL(type));
      if (OB_SUCC(ret) && type->is_obj_type()) {
        ObObj &obj = record->get_element()[i];
        OZ (ObUserDefinedType::destruct_objparam(*record->get_allocator(), obj, nullptr));
      }
      OZ (type->deserialize(resolve_ctx, *record->get_allocator(), src, src_len, src_pos, dst));
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
    ObArenaAllocator tmp_allocator(GET_PL_MOD_STRING(PL_MOD_IDX::OB_PL_ARENA), OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID());
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
 * 为了ObPLComposite及其继承类和LLVM之间的内存映射，本函数不能实现虚函数
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
 * 我们约定一个原则：
 * 1、所有Collection内部的data域的ObObj数组（包括sort域和key域的内存）的内存都必须由该Collection自己的allocator分配，而不允许是其他任何allocator；
 * 2、如果data域里是基础数据类型，那么内存也应由Collection自己的allocator分配；
 * 3、如果data域是record，那么该record本身的内存同样由Collection自己的allocator分配；record里的基础数据类型的内存同样由Collection自己的allocator分配；
 * 4、如果data域里是子Collection，那么该子Collection数据结构本身由父Collection的allocator分配，子Collection的内存管理递归遵循此约定。
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
    // 当有赋值的时候，first和last都会被置成该值，所以需要从头遍历一遍。
    // 为啥需要这个，是为了优化性能，比如现在first是4，这个时候2的赋值赋值的。
    // 所以需要从头遍历。但是delete的时候，不会做这个操作，所以，只要判断first对应的是否有效又可以了。
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
 * Collection里的数据有多重情况需要分开处理：
 * 1、简单类型，element_不是extend，col_cnt_是1：直接按顺序写进Obj即可
 * 2、复杂类型，element_是extend，col_cnt_是1：此时两种情况：
 *        a、可能是个Record，里面只有一个元素：需要构造Record空间
 *        b、可能是个Collection，直接写进Obj即可
 * 3、复杂类型，element_是extend，col_cnt_大于1：说明是个Record：需要构造Record空间
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
      if (data_obj.is_ext()) { //已经是extend，说明该空间已经分配了内存，直接在内存写即可
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
      } else if (data_obj.is_null()) { //还没有分配空间，需要分配
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
