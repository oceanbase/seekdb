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

#define USING_LOG_PREFIX STORAGE_FTS
#include "data_plane/fts/dict/ob_dic_loader.h"
#include "storage/fts/dict/ob_dic_lock.h"
namespace oceanbase
{
namespace storage
{
/**
* -----------------------------------ObDicLoader-----------------------------------
*/
int ObDicLoader::load_dictionary_in_trans(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the dic loader is not initialized", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < dic_tables_info_.count(); ++i) {
      int64_t array_size = dic_tables_info_.at(i).array_size_;
      const char *table_name = dic_tables_info_.at(i).table_name_;
      common::ObSqlString query_string;
      common::ObSqlString columns;
      common::ObSqlString values;
      share::ObDMLSqlSplicer dml;
      int64_t pos = 0;
      while (array_size > 0 && OB_SUCC(ret)) {
        columns.reuse();
        query_string.reuse();
        for (int64_t j = 0; OB_SUCC(ret) && j < DEFAULT_BATCH_SIZE && j < array_size; ++j, ++pos) {
          ObDicItem item;
          dml.reuse();
          if (OB_FAIL(get_dic_item(i, pos, item))) {
          } else if (OB_FAIL(fill_dic_item(item, dml))){
          } else {
            if (0 == j) {
              if (OB_FAIL(dml.splice_column_names(columns))) {
              } else if (OB_FAIL(query_string.append_fmt("INSERT INTO %s (%s) VALUES", 
                          table_name, columns.ptr()))) {
              }
            }

            if (OB_SUCC(ret)) {
              values.reset();
              if (OB_FAIL(dml.splice_values(values))) {
              } else if (OB_FAIL(query_string.append_fmt("%s(%s)",
                      0 == j ? " " : " , ", values.ptr()))) {
              }
            }
          }
        }
        array_size -= DEFAULT_BATCH_SIZE;
        if (OB_SUCC(ret)) {
          int64_t affected_rows = 0;
          if (OB_FAIL(trans.write(query_string.ptr(), affected_rows))) {
          } else if (OB_UNLIKELY(((array_size > 0) && affected_rows != DEFAULT_BATCH_SIZE) || (affected_rows <= 0))) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("invalid affected rows", K(ret), K(affected_rows));
          }
        }
      }
    }
  }
  return ret;
}

int ObDicLoader::try_load_dictionary_in_trans(ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the dic loader is not initialized", K(ret));
  } else {
    if (!is_load_) {
      bool is_need_load_dic = false;
      if (OB_FAIL(check_need_load_dic(is_need_load_dic))) {
      } else if (is_need_load_dic) {
        if (OB_FAIL(ObDicLock::lock_dic_tables_in_trans(*this,
                                                        transaction::tablelock::EXCLUSIVE, 
                                                        trans))) {
        }
        if (OB_SUCC(ret)) {
          if (OB_FALSE_IT(is_need_load_dic = false)) {
          } else if (OB_FAIL(check_need_load_dic(is_need_load_dic))) {
          } else if (is_need_load_dic) {
            if (OB_FAIL(load_dictionary_in_trans( trans))) {
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        is_load_ = true;
      }
    }
  }
  return ret;
}

int ObDicLoader::try_load_dictionary_in_trans()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("the dic loader is not initialized", K(ret));
  } else {
    if (!is_load_) {
      bool is_need_load_dic = false;
      if (OB_FAIL(check_need_load_dic(is_need_load_dic))) {
      } else if (is_need_load_dic) {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("dictionary loading requires a caller-owned transaction", K(ret));
      } else {
        is_load_ = true;
      }
    }
  }
  return ret; 
}

int ObDicLoader::check_need_load_dic(bool &is_need_load_dic)
{
  int ret = OB_SUCCESS;
  // we keep the code here even though we don't load data into system table anymore.
  is_need_load_dic = false;
  return ret;
}

/**
* -----------------------------------ObDicLoaderHandle-----------------------------------
*/
ObDicLoaderHandle &ObDicLoaderHandle::operator =(const ObDicLoaderHandle &other)
{
  if (this != &other) {
    reset();
    if (OB_NOT_NULL(other.loader_)) {
      loader_ = other.loader_;
      loader_->inc_ref();
    }
  }
  return *this;
}

void ObDicLoaderHandle::reset()
{
  if (nullptr != loader_) {
    const int64_t ref_cnt = loader_->dec_ref();
    if (0 == ref_cnt) {
      ObMemAttr attr("dic_loader");
      OB_DELETE(ObDicLoader, attr, loader_);
    }
    loader_ = nullptr;
  }
}

int ObDicLoaderHandle::set_loader(ObDicLoader *loader)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(loader)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(loader));
  } else {
    reset();
    loader_ = loader;
    loader_->inc_ref();
  }
  return ret;
}
} // end storage
} // end oceanbase
