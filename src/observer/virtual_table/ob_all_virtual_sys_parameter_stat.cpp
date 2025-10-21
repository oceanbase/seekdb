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

#include "ob_all_virtual_sys_parameter_stat.h"
#include "observer/ob_server_utils.h"
#include "src/sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;

namespace observer
{
ObAllVirtualSysParameterStat::ObAllVirtualSysParameterStat()
    : ObVirtualTableIterator(), sys_iter_(), tenant_config_(), tenant_iter_()
{
}

ObAllVirtualSysParameterStat::~ObAllVirtualSysParameterStat()
{
  reset();
}

int ObAllVirtualSysParameterStat::inner_open()
{
  int ret = OB_SUCCESS;
  sys_iter_ = GCONF.get_container().begin();
  tenant_config_.set_config(TENANT_CONF(OB_SYS_TENANT_ID));
  if (tenant_config_.is_valid()) {
    tenant_iter_ = tenant_config_->get_container().begin();
  } else {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "failed to get tenant config", K(ret));
  }
  return ret;
}

void ObAllVirtualSysParameterStat::reset()
{
  sys_iter_ = GCONF.get_container().begin();
  tenant_config_.set_config(TENANT_CONF(OB_SYS_TENANT_ID));
  if (tenant_config_.is_valid()) {
    tenant_iter_ = tenant_config_->get_container().begin();
  } else {
    SERVER_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "failed to get tenant config");
  }
}

int ObAllVirtualSysParameterStat::inner_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(inner_sys_get_next_row(row))) {
  }
  return ret;
}

int ObAllVirtualSysParameterStat::inner_sys_get_next_row(ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  const ObConfigContainer &container = GCONF.get_container();
  if (sys_iter_ == container.end()) {
    ret = OB_ITER_END;
  } else {
    ObObj *cells = cur_row_.cells_;
    ObString ipstr;
    if (OB_UNLIKELY(NULL == cells)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(ERROR, "cur row cell is NULL", K(ret));
    } else if (OB_FAIL(ObServerUtils::get_server_ip(allocator_, ipstr))) {
      SERVER_LOG(ERROR, "get server ip failed", K(ret));
    } else {
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
            cells[i].set_varchar(sys_iter_->first.str());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case DATA_TYPE: {
            cells[i].set_varchar(sys_iter_->second->data_type());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case VALUE: {
            cells[i].set_varchar(sys_iter_->second->str());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case INFO: {
            cells[i].set_varchar(sys_iter_->second->info());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case SECTION: {
            cells[i].set_varchar(sys_iter_->second->section());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case SCOPE: {
            cells[i].set_varchar(sys_iter_->second->scope());
            cells[i].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case SOURCE: {
           cells[i].set_varchar(sys_iter_->second->source());
           cells[i].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
           break;
          }
        case EDIT_LEVEL: {
           cells[i].set_varchar(sys_iter_->second->edit_level());
           cells[i].set_collation_type(
               ObCharset::get_default_collation(ObCharset::get_default_charset()));
           break;
          }
        case DEFAULT_VALUE: {
            cells[i].set_varchar(sys_iter_->second->default_str());
            cells[i].set_collation_type(
              ObCharset::get_default_collation(ObCharset::get_default_charset()));
            break;
          }
        case ISDEFAULT: {
            int isdefault = sys_iter_->second->is_default(sys_iter_->second->str(),sys_iter_->second->default_str(),sizeof(sys_iter_->second->default_str())) ? 1 : 0;
            cells[i].set_int(isdefault);
            break;
          }
        default : {
            // TODO: Version compatibility, extra columns do not cause errors
            // ret = OB_ERR_UNEXPECTED;
            // SERVER_LOG(WARN, "unexpected column id", K(col_id), K(i), K(ret));
	    cells[i].set_null();
            break;
          }
        }
      } // end for
      if (OB_SUCC(ret)) {
        row = &cur_row_;
        ++sys_iter_;
      }
    }
  }
  return ret;
}

} // namespace observer
} // namespace oceanbase
