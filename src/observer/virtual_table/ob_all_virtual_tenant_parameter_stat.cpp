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

#include "ob_all_virtual_tenant_parameter_stat.h"
#include "observer/ob_server_utils.h"
#include "observer/ob_server_struct.h"
#include "observer/ob_sql_client_decorator.h"

namespace oceanbase
{
using namespace common;
using namespace share;

namespace observer
{

ObAllVirtualTenantParameterStat::ObAllVirtualTenantParameterStat() :
    inited_(false),
    show_seed_(false),
    sys_iter_()
{
}

ObAllVirtualTenantParameterStat::~ObAllVirtualTenantParameterStat()
{
  reset();
}

int ObAllVirtualTenantParameterStat::init(const bool show_seed)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    SERVER_LOG(WARN, "init twice", KR(ret), K(inited_));
  } else if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "unexpected null of omt", KR(ret), K(GCTX.omt_));
  } else if (FALSE_IT(show_seed_ = show_seed)) {
  } else {
    sys_iter_ = GCONF.get_container().begin();
  }

  if (OB_SUCC(ret)) {
    inited_ = true;
  }
  return ret;
}


int ObAllVirtualTenantParameterStat::inner_open()
{
  return OB_SUCCESS;
}

void ObAllVirtualTenantParameterStat::reset()
{
  ObVirtualTableIterator::reset();
  inited_ = false;
  show_seed_ = false;
  sys_iter_ = GCONF.get_container().begin();
}

int ObAllVirtualTenantParameterStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    SERVER_LOG(WARN, "not inited", K(inited_), KR(ret));
  } else {
    if (OB_SUCC(inner_sys_get_next_row(row))) {
    }
  }
  return ret;
}

int ObAllVirtualTenantParameterStat::inner_sys_get_next_row(common::ObNewRow *&row)
{
  /*cluster parameter does not belong to any tenant*/
  return fill_row_(row, sys_iter_, GCONF.get_container(), NULL);
}

int ObAllVirtualTenantParameterStat::fill_row_(common::ObNewRow *&row,
    CfgIter &iter,
    const ObConfigContainer &cfg_container,
    const uint64_t *tenant_id_ptr /*NULL means not to output tenant_id*/)
{
  int ret = OB_SUCCESS;
  if (iter == cfg_container.end()) {
    ret = OB_ITER_END;
  } else {
    ObObj *cells = cur_row_.cells_;
    ObString ipstr;
    if (OB_UNLIKELY(NULL == cells)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", KR(ret));
    } else if (OB_FAIL(ObServerUtils::get_server_ip(allocator_, ipstr))) {
      SERVER_LOG(ERROR, "get server ip failed", KR(ret));
    } else {
      if (iter == cfg_container.end()) {
        ret = OB_ITER_END;
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < output_column_ids_.count(); ++i) {
        const uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
          case ZONE: {
            cells[i].set_varchar(GCONF.zone);
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SERVER_TYPE: {
            cells[i].set_varchar("observer");
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SERVER_IP: {
            cells[i].set_varchar(ipstr);
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SERVER_PORT: {
            cells[i].set_int(GCONF.self_addr_.get_port());
            break;
          }
          case NAME: {
            cells[i].set_varchar(iter->first.str());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case DATA_TYPE: {
            cells[i].set_varchar(iter->second->data_type());
            cells[i].set_collation_type(
                  ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case VALUE: {
            if (0 == ObString("compatible").case_compare(iter->first.str())) {
              const uint64_t tenant_id = OB_SYS_TENANT_ID;
              uint64_t data_version = 0;
              char *dv_buf = NULL;
              if (GET_MIN_DATA_VERSION(tenant_id, data_version) != OB_SUCCESS) {
                // `compatible` is used for tenant compatibility,
                // default value should not be used when `compatible` is not
                // loaded yet.
                cells[i].set_varchar("0.0.0.0");
              } else if (OB_ISNULL(dv_buf = (char *)allocator_->alloc(OB_SERVER_VERSION_LENGTH))) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
                SERVER_LOG(ERROR, "fail to alloc buf", K(ret), K(tenant_id),
                           K(OB_SERVER_VERSION_LENGTH));
              } else if (OB_INVALID_INDEX ==
                         VersionUtil::print_version_str(
                             dv_buf, OB_SERVER_VERSION_LENGTH, data_version)) {
                ret = OB_INVALID_ARGUMENT;
                SERVER_LOG(ERROR, "fail to print data_version", K(ret),
                           K(tenant_id), K(data_version));
              } else {
                cells[i].set_varchar(dv_buf);
              }
            } else {
              cells[i].set_varchar(iter->second->str());
            }
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case INFO: {
            cells[i].set_varchar(iter->second->info());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SECTION: {
            cells[i].set_varchar(iter->second->section());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SCOPE: {
            cells[i].set_varchar(iter->second->scope());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case SOURCE: {
            cells[i].set_varchar(iter->second->source());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case EDIT_LEVEL: {
            cells[i].set_varchar(iter->second->edit_level());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case TENANT_ID: {
              cells[i].set_int(OB_SYS_TENANT_ID);
            break;
          }
          case DEFAULT_VALUE: {
            cells[i].set_varchar(iter->second->default_str());
            cells[i].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
          case ISDEFAULT: {
            int isdefault = iter->second->is_default(iter->second->str(),iter->second->default_str(),sizeof(iter->second->default_str())) ? 1 : 0;
            cells[i].set_int(isdefault);
            break;
          }
          default : {
            // skip unknown column for version compatibility
            cells[i].set_null();
            SERVER_LOG(WARN, "unknown column id", K(col_id), K(i), K(ret),
                K(OB_ALL_VIRTUAL_TENANT_PARAMETER_STAT_TNAME));
            break;
          }
        }
      } // end for
      if (OB_SUCC(ret)) {
        row = &cur_row_;
        ++iter;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase

