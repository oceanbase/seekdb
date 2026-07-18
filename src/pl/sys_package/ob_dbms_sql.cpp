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

#include "ob_dbms_sql.h"
#include "pl/ob_pl_package.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/parser/ob_parser.h"
#include "sql/engine/expr/ob_expr_pl_associative_index.h"

namespace oceanbase
{
using namespace common;
using namespace common::number;
using namespace sql;

namespace pl
{

int ObDbmsInfo::init()
{
  int ret = OB_SUCCESS;
  if (!define_columns_.created() &&
             OB_FAIL(define_columns_.create(common::hash::cal_next_prime(32),
                                           ObModIds::OB_HASH_BUCKET, ObModIds::OB_HASH_NODE))) {
    SQL_ENG_LOG(WARN, "create sequence current value map failed", K(ret));
  } else if (!define_arrays_.created() &&
      OB_FAIL(define_arrays_.create(common::hash::cal_next_prime(32),
                                    ObModIds::OB_HASH_BUCKET, ObModIds::OB_HASH_NODE))) {
    SQL_ENG_LOG(WARN, "create sequence current value map failed", K(ret));
  } else { /*do nothing*/ }
  return ret;
}

void ObDbmsInfo::reset()
{
  for (int64_t i = 0; i < bind_params_.count(); ++i) {
    (void)ObUserDefinedType::destruct_obj(bind_params_.at(i).param_value_);
  }
  param_names_.reset();
  into_names_.reset();
  bind_params_.reset();
  exec_params_.reset();
  sql_stmt_.reset();
  define_columns_.reuse();
  fields_.reset();
  if (nullptr != entity_) {
    DESTROY_CONTEXT(entity_);
    entity_ = nullptr;
  }
}


int ObDbmsInfo::deep_copy_field_columns(ObIAllocator& allocator,
                                        const common::ColumnsFieldIArray* src_fields,
                                        common::ColumnsFieldArray &dst_fields)
{
  int ret = OB_SUCCESS;
  dst_fields.reset();
  dst_fields.set_allocator(&allocator);
  if (OB_ISNULL(src_fields)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("can't copy null fields", K(ret));
  } else if (src_fields->count() < 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("src fields is null.", K(ret), K(src_fields->count()));
  } else if (0 == src_fields->count() ) {
    // do nothing
    // SELECT * INTO OUTFILE return null field
  } else if (OB_FAIL(dst_fields.reserve(src_fields->count()))) {
    LOG_WARN("fail to reserve column fields",
             K(ret), K(dst_fields.count()), K(src_fields->count()));
  } else {
    for (int64_t i = 0 ; OB_SUCC(ret) && i < src_fields->count(); ++i) {
      ObField tmp_field;
      if (OB_FAIL(tmp_field.deep_copy(src_fields->at(i), &allocator))) {
        LOG_WARN("deep copy field failed", K(ret));
      } else if (OB_FAIL(dst_fields.push_back(tmp_field))) {
        LOG_WARN("push back field param failed", K(ret));
      } else { }
    }
  }
  return ret;
}


int ObDbmsInfo::init_params(int64_t param_count)
{
  int ret = OB_SUCCESS;
  ObIAllocator *alloc = NULL;
  OV (OB_NOT_NULL(entity_), OB_NOT_INIT, ps_sql_, param_count);
  OX (alloc = &entity_->get_arena_allocator());
  CK (OB_NOT_NULL(alloc));
  OX (param_names_.reset());
  OX (param_names_.set_allocator(alloc));
  OZ (param_names_.init(param_count), ps_sql_, param_count);
  OX (bind_params_.reset());
  OX (bind_params_.set_allocator(alloc));
  OZ (bind_params_.init(param_count), ps_sql_, param_count);
  OX (exec_params_.~Ob2DArray());
  OX (new (&exec_params_) ParamStore(ObWrapperAllocator(alloc)));
  return ret;
}





int ObDbmsInfo::add_param_name(ObString &clone_name)
{
  return param_names_.push_back(clone_name);
}


int ObDbmsCursorInfo::close(ObSQLSessionInfo &session, bool is_cursor_reuse, bool is_dbms_reuse)
{
  int ret = OB_SUCCESS;
  OZ (ObPLCursorInfo::close(session, is_cursor_reuse));
  reset_private();
  is_dbms_reuse ? ObDbmsInfo::reuse() : ObDbmsInfo::reset();
  return ret;
}

int ObDbmsCursorInfo::init()
{
  return ObDbmsInfo::init();
}

void ObDbmsCursorInfo::reset_private()
{
  affected_rows_ = -1;
}


void ObDbmsCursorInfo::reset()
{
  reset_private();
  ObDbmsInfo::reset();
  ObPLCursorInfo::reset();
}


int ObDbmsCursorInfo::prepare_entity(sql::ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObPLCursorInfo::prepare_entity(session, get_dbms_entity()))) {
    LOG_WARN("prepare dbms info entity fail.", K(ret), K(get_id()));
  } else if (OB_FAIL(
      ObPLCursorInfo::prepare_entity(session, get_cursor_entity()))) {
    LOG_WARN("prepare cursor entity fail.", K(ret), K(get_id()));
  } else { /* do nothing */ }
  return ret;
}






}
}
