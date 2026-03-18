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


#include "table_accessor.h"

namespace oceanbase
{
namespace logservice
{
namespace coordinator
{
TableAccessor::ServerZoneNameCache TableAccessor::SERVER_ZONE_NAME_CACHE;
using namespace common;

int get_tenant_server_list(common::ObIArray<common::ObAddr> &tenant_server_list)
{
  LC_TIME_GUARD(1_s);
  int ret = OB_SUCCESS;
  tenant_server_list.reset();

  return ret;
}

LsElectionReferenceInfoRow::LsElectionReferenceInfoRow(const uint64_t tenant_id, const share::ObLSID &ls_id)
: tenant_id_(tenant_id), ls_id_(ls_id), exec_tenant_id_(get_private_table_exec_tenant_id(tenant_id_)) {}

LsElectionReferenceInfoRow::~LsElectionReferenceInfoRow()
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(*this)
  int ret = OB_SUCCESS;
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "LsElectionReferenceInfoRow destroyed with begined transaction");
    if (CLICK_FAIL(trans_.end(false))) {
      COORDINATOR_LOG_(WARN, "LsElectionReferenceInfoRow roll back transaction failed");
    }
  }
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::begin_()
{
  LC_TIME_GUARD(1_s);
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(trans_.start(GCTX.sql_proxy_, exec_tenant_id_))) {
    COORDINATOR_LOG(WARN, "fail to start trans", KR(ret), K(*this));
  } else {
    COORDINATOR_LOG(INFO, "success to start trans", KR(ret), K(*this));
  }
  return ret;
}

int LsElectionReferenceInfoRow::end_(const bool true_to_commit)
{
  LC_TIME_GUARD(1_s);
  return trans_.end(true_to_commit);
}

int LsElectionReferenceInfoRow::change_zone_priority(const ObArray<ObArray<ObStringHolder>> &zone_list_list)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(zone_list_list)
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(start_and_read_())) {
    COORDINATOR_LOG_(WARN, "failed when start trans, read row, convert info");
  } else if (CLICK_FAIL(row_for_user_.element<2>().assign(zone_list_list))) {
    COORDINATOR_LOG_(WARN, "fail to assign zone list list");
  } else if (CLICK_FAIL(write_and_commit_())) {
    COORDINATOR_LOG_(WARN, "failed when convert info, write row, end trans");
  } else {
    COORDINATOR_LOG_(INFO, "change_zone_priority");
  }
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "transaction execute failed");
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = end_(false))) {
      COORDINATOR_LOG_(WARN, "fail to roll back transaction");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::change_manual_leader(const common::ObAddr &manual_leader_server)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(manual_leader_server)
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(start_and_read_())) {
    COORDINATOR_LOG_(WARN, "failed when start trans, read row, convert info");
  } else if (FALSE_IT(row_for_user_.element<3>() = manual_leader_server)) {
  } else if (CLICK_FAIL(write_and_commit_())) {
    COORDINATOR_LOG_(WARN, "failed when convert info, write row, end trans");
  } else {
    COORDINATOR_LOG_(INFO, "change_manual_leader");
  }
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "transaction execute failed");
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = end_(false))) {
      COORDINATOR_LOG_(WARN, "fail to roll back transaction");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::add_server_to_blacklist(const common::ObAddr &server, InsertElectionBlacklistReason reason)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(server), K(reason)
  int ret = OB_SUCCESS;
  if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    COORDINATOR_LOG_(WARN, "server is invalid or reason is empty");
  } else {
    if (CLICK_FAIL(start_and_read_())) {
      COORDINATOR_LOG_(WARN, "failed when start trans, read row, convert info");
    } else {
      for (int64_t idx = 0; idx < row_for_user_.element<4>().count() && OB_SUCC(ret); ++idx) {
        if (row_for_user_.element<4>().at(idx).element<0>() == server) {
          ret = OB_ENTRY_EXIST;
          break;
        }
      }
      if (OB_SUCC(ret)) {
        if (CLICK_FAIL(row_for_user_.element<4>().push_back(ObTuple<ObAddr, ObStringHolder>()))) {
          COORDINATOR_LOG_(WARN, "failed to create new tuple for reason");
        } else if (FALSE_IT(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<0>() = server)) {
        } else if (CLICK_FAIL(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<1>().assign(to_cstring(reason)))) {
          COORDINATOR_LOG_(WARN, "copy reason failed");
        } else if (CLICK_FAIL(write_and_commit_())) {
          COORDINATOR_LOG_(WARN, "failed when convert info, write row, end trans");
        } else {
          COORDINATOR_LOG_(INFO, "add_server_to_blacklist");
        }
      }
    }
  }
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "transaction execute failed");
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = end_(false))) {
      COORDINATOR_LOG_(WARN, "fail to roll back transaction");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::delete_server_from_blacklist(const common::ObAddr &server)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(server)
  int ret = OB_SUCCESS;
  if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    COORDINATOR_LOG_(WARN, "server is invalid");
  } else {
    if (CLICK_FAIL(start_and_read_())) {
      COORDINATOR_LOG_(WARN, "failed when start trans, read row, convert info");
    } else {
      ObArray<ObTuple<ObAddr, ObStringHolder>> &old_array = row_for_user_.element<4>();
      ObArray<ObTuple<ObAddr, ObStringHolder>> new_array;
      for (int64_t idx = 0; idx < old_array.count() && OB_SUCC(ret); ++idx) {
        if (old_array.at(idx).element<0>() != server) {
          if (CLICK_FAIL(new_array.push_back(old_array.at(idx)))) {
            COORDINATOR_LOG_(WARN, "push tuple to new array failed");
          }
        }
      }
      if (old_array.count() == new_array.count()) {
        ret = OB_ENTRY_NOT_EXIST;
        COORDINATOR_LOG_(WARN, "this server not in blacklist column");
      } else if (CLICK_FAIL(old_array.assign(new_array))) {
        COORDINATOR_LOG_(WARN, "replace old array with new array failed");
      } else if (CLICK_FAIL(write_and_commit_())) {
        COORDINATOR_LOG_(WARN, "failed when convert info, write row, end trans");
      } else {
        COORDINATOR_LOG_(INFO, "delete_server_from_blacklist");
      }
    }
  }
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "transaction execute failed");
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = end_(false))) {
      COORDINATOR_LOG_(WARN, "fail to roll back transaction");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::set_or_replace_server_in_blacklist(
    const common::ObAddr &server,
    InsertElectionBlacklistReason reason)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(server), K(reason)
  int ret = OB_SUCCESS;
  if (!server.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    COORDINATOR_LOG_(WARN, "server is invalid or reason is empty");
  } else if (CLICK_FAIL(start_and_read_())) {
    COORDINATOR_LOG_(WARN, "failed when start trans, read row, convert info");
  } else if (CLICK_FAIL(set_user_row_for_specific_reason_(server, reason))) {
    COORDINATOR_LOG_(WARN, "set_user_row_for_specific_reason_ failed");
  } else if (CLICK_FAIL(write_and_commit_())) {
    COORDINATOR_LOG_(WARN, "failed when convert info, write row, end trans");
  } else {
    ObCStringHelper helper;
    COORDINATOR_LOG_(INFO, "set_or_replace_server_in_blacklist", K(server), "reason", to_cstring(reason));
  }
  if (trans_.is_started()) {
    COORDINATOR_LOG_(WARN, "transaction execute failed");
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = end_(false))) {
      COORDINATOR_LOG_(WARN, "fail to roll back transaction");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::set_user_row_for_specific_reason_(
    const common::ObAddr &server,
    InsertElectionBlacklistReason reason)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret), K(server), K(reason)
  int ret = OB_SUCCESS;
  const char *reason_str = to_cstring(reason);
  int64_t server_idx = -1;
  int64_t number_of_the_reason = 0;
  for (int64_t idx = 0; idx < row_for_user_.element<4>().count(); ++idx) {
    if (row_for_user_.element<4>().at(idx).element<0>() == server) {
      server_idx = idx;
    }
    if (row_for_user_.element<4>().at(idx).element<1>() == reason_str) {
      number_of_the_reason += 1;
    }
  }
  if (-1 != server_idx && 1 == number_of_the_reason) {
    ret = OB_ENTRY_EXIST;
  } else {
    ObArray<ObTuple<ObAddr, ObStringHolder>> &old_array = row_for_user_.element<4>();
    ObArray<ObTuple<ObAddr, ObStringHolder>> new_array;
    for (int64_t idx = 0; idx < old_array.count() && OB_SUCC(ret); ++idx) {
      if (old_array.at(idx).element<1>() != reason_str) {
        if (CLICK_FAIL(new_array.push_back(old_array.at(idx)))) {
          COORDINATOR_LOG_(WARN, "push tuple to new array failed");
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (CLICK_FAIL(old_array.assign(new_array))) {
      COORDINATOR_LOG_(WARN, "replace old array with new array failed");
    } else if (CLICK_FAIL(row_for_user_.element<4>().push_back(ObTuple<ObAddr, ObStringHolder>()))) {
      COORDINATOR_LOG_(WARN, "failed to create new tuple for reason");
    } else if (FALSE_IT(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<0>() = server)) {
    } else if (CLICK_FAIL(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<1>().assign(to_cstring(reason)))) {
      COORDINATOR_LOG_(WARN, "copy reason failed");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::start_and_read_()
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret)
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(begin_())) {
    COORDINATOR_LOG_(WARN, "start transaction failed");
  } else if (CLICK_FAIL(get_row_from_table_())) {
    COORDINATOR_LOG_(WARN, "read row from table failed");
  } else if (CLICK_FAIL(convert_table_info_to_user_info_())) {
    COORDINATOR_LOG_(WARN, "convert table info to user info failed");
  } else {
    COORDINATOR_LOG_(INFO, "read __all_ls_election_reference_info");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::write_and_commit_()
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER K(*this), KR(ret)
  int ret = OB_SUCCESS;
  if (CLICK_FAIL(convert_user_info_to_table_info_())) {
    COORDINATOR_LOG_(WARN, "convert user info to table info failed");
  } else if (CLICK_FAIL(update_row_to_table_())) {
    COORDINATOR_LOG_(WARN, "update column failed");
  } else if (CLICK_FAIL(end_(true))) {
    COORDINATOR_LOG_(WARN, "commit change failed");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::convert_user_info_to_table_info_()
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(*this)
  int ret = OB_SUCCESS;
  row_for_user_.element<0>() = tenant_id_;
  row_for_user_.element<1>() = ls_id_;
  row_for_table_.element<0>() = tenant_id_;
  row_for_table_.element<1>() = ls_id_.id();
  ObArray<ObStringHolder> temp_holders;
  for (int64_t idx = 0; idx < row_for_user_.element<2>().count() && OB_SUCC(ret); ++idx) {
    char buffer[STACK_BUFFER_SIZE] = {0};
    if (CLICK_FAIL(ObTableAccessHelper::join_string_array_with_begin_end(buffer, STACK_BUFFER_SIZE, "", "", ",", row_for_user_.element<2>().at(idx)))) {
      COORDINATOR_LOG_(WARN, "join string failed");
    } else if (CLICK_FAIL(temp_holders.push_back(ObStringHolder()))) {
      COORDINATOR_LOG_(WARN, "get where condition string failed");
    } else if (CLICK_FAIL(temp_holders[temp_holders.count() - 1].assign(ObString(strlen(buffer), buffer)))) {
      COORDINATOR_LOG_(WARN, "get where condition string failed");
    }
  }
  if (OB_SUCC(ret)) {
    char buffer[STACK_BUFFER_SIZE] = {0};
    if (CLICK_FAIL(ObTableAccessHelper::join_string_array_with_begin_end(buffer, STACK_BUFFER_SIZE, "", "", ";", temp_holders))) {
      COORDINATOR_LOG_(WARN, "join string failed");
    } else if (CLICK_FAIL(row_for_table_.element<2>().assign(ObString(ObString(strlen(buffer), buffer))))) {
      COORDINATOR_LOG_(WARN, "create StringHolder failed");
    } else if (FALSE_IT(memset(buffer, 0, STACK_BUFFER_SIZE))) {
    } else if (CLICK_FAIL(row_for_user_.element<3>().ip_port_to_string(buffer, STACK_BUFFER_SIZE))) {
      COORDINATOR_LOG_(WARN, "ip port to string failed");
    } else if (CLICK_FAIL(row_for_table_.element<3>().assign(ObString(strlen(buffer), buffer)))) {
      COORDINATOR_LOG_(WARN, "create ObStingHolder failed");
    } else {
      ObArray<ObStringHolder> remove_addr_and_reason_list;
      for (int64_t idx = 0; idx < row_for_user_.element<4>().count() && OB_SUCC(ret); ++idx) {
        ObStringHolder ip_port_holder;
        if (FALSE_IT(memset(buffer, 0, STACK_BUFFER_SIZE))) {
        } else if (CLICK_FAIL(row_for_user_.element<4>().at(idx).element<0>().ip_port_to_string(buffer, STACK_BUFFER_SIZE))) {
          COORDINATOR_LOG_(WARN, "ip port to string failed");
        } else if (CLICK_FAIL(ip_port_holder.assign(ObString(strlen(buffer), buffer)))) {
          COORDINATOR_LOG_(WARN, "create ObStringHolder failed");
        } else if (FALSE_IT(memset(buffer, 0, STACK_BUFFER_SIZE))) {
        } else if (CLICK_FAIL(ObTableAccessHelper::join_string(buffer, STACK_BUFFER_SIZE, "", ip_port_holder.get_ob_string(), ObString("("), row_for_user_.element<4>().at(idx).element<1>(), ObString(")")))) {
          COORDINATOR_LOG_(WARN, "join string failed");
        } else if (CLICK_FAIL(remove_addr_and_reason_list.push_back(ObStringHolder()))) {
          COORDINATOR_LOG_(WARN, "push back new string holder failed");
        } else if (CLICK_FAIL(remove_addr_and_reason_list.at(remove_addr_and_reason_list.count() - 1).assign(ObString(strlen(buffer), buffer)))) {
          COORDINATOR_LOG_(WARN, "create remove_addr_and_reason string holder failed");
        }
      }
      if (OB_SUCC(ret)) {
        if (FALSE_IT(memset(buffer, 0, STACK_BUFFER_SIZE))) {
        } else if (CLICK_FAIL(ObTableAccessHelper::join_string_array_with_begin_end(buffer, STACK_BUFFER_SIZE, "", "", ";", remove_addr_and_reason_list))) {
        } else if (CLICK_FAIL(row_for_table_.element<4>().assign(ObString(strlen(buffer), buffer)))) {
          COORDINATOR_LOG_(WARN, "create remove_addr_and_reason string holder in row failed");
        } else {
          COORDINATOR_LOG_(INFO, "LsElectionReferenceInfoRow setted success");
        }
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::convert_table_info_to_user_info_()
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(*this)
  int ret = OB_SUCCESS;
  row_for_table_.element<0>() = tenant_id_;
  row_for_table_.element<1>() = ls_id_.id();
  row_for_user_.element<0>() = tenant_id_;
  row_for_user_.element<1>() = ls_id_;
  row_for_user_.element<2>().reset();
  ObArray<ObStringHolder> split_by_semicolon;
  if (!row_for_table_.element<2>().empty() && CLICK_FAIL(ObTableAccessHelper::split_string_by_char(row_for_table_.element<2>(), ';', split_by_semicolon))) {
    COORDINATOR_LOG_(WARN, "split zone priority by semicolon failed");
  } else {
    for (int64_t idx = 0; idx < split_by_semicolon.count() && OB_SUCC(ret); ++idx) {
      if (CLICK_FAIL(row_for_user_.element<2>().push_back(ObArray<ObStringHolder>()))) {
        COORDINATOR_LOG_(WARN, "push new array to array of array failed");
      } else if (CLICK_FAIL(ObTableAccessHelper::split_string_by_char(split_by_semicolon.at(idx), ',', row_for_user_.element<2>().at(row_for_user_.element<2>().count() - 1)))) {
        COORDINATOR_LOG_(WARN, "split string by ',' failed");
      }
    }
    if (OB_SUCC(ret)) {
      if (!row_for_table_.element<3>().empty() && CLICK_FAIL(row_for_user_.element<3>().parse_from_string(row_for_table_.element<3>().get_ob_string()))) {
        COORDINATOR_LOG_(WARN, "get ObAddr from ObString failed");
      } else {
        ObArray<ObStringHolder> split_by_comma;
        if (!row_for_table_.element<4>().empty() && CLICK_FAIL(ObTableAccessHelper::split_string_by_char(row_for_table_.element<4>(), ';', split_by_comma))) {
          COORDINATOR_LOG_(WARN, "split blacklist by ',' failed");
        } else {
          for (int64_t idx = 0; idx < split_by_comma.count() && OB_SUCC(ret); ++idx) {
            ObString reason = split_by_comma.at(idx).get_ob_string();
            ObString server = reason.split_on('(');
            reason = reason.split_on(')');
            if (server.empty() || reason.empty()) {
              ret = OB_ERR_UNEXPECTED;
              COORDINATOR_LOG_(ERROR, "should not get empty reason or server", K(server), K(reason));
            } else if (CLICK_FAIL(row_for_user_.element<4>().push_back(ObTuple<ObAddr, ObStringHolder>()))) {
              COORDINATOR_LOG_(WARN, "create new addr and reason tuple failed", K(server), K(reason));
            } else if (CLICK_FAIL(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<0>().parse_from_string(server))) {
              COORDINATOR_LOG_(WARN, "fail to resolve server ip port", K(server), K(reason));
            } else if (CLICK_FAIL(row_for_user_.element<4>().at(row_for_user_.element<4>().count() - 1).element<1>().assign(reason))) {
              COORDINATOR_LOG_(WARN, "fail to copy removed reason string holder", K(server), K(reason));
            }
          }
        }
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int LsElectionReferenceInfoRow::update_row_to_table_()
{
  int ret = OB_SUCCESS;
  return ret;
}

int LsElectionReferenceInfoRow::get_row_from_table_()
{
  int ret = OB_SUCCESS;
  return ret;
}

int TableAccessor::get_self_zone_name(ObStringHolder &zone_name_holder)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(zone_name_holder)
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.config_)) {
    ret = OB_INVALID_ARGUMENT;
    COORDINATOR_LOG_(WARN, "invalid argument");
  } else if (OB_FAIL(zone_name_holder.assign(GCTX.config_->zone.str()))) {
    COORDINATOR_LOG_(WARN, "assign failed");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int TableAccessor::get_self_zone_region(const ObStringHolder &zone_name_holder,
                                        ObStringHolder &region_name_holder)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret)
  int ret = OB_SUCCESS;
  if (OB_FAIL(region_name_holder.assign(DEFAULT_REGION_NAME))) {
    COORDINATOR_LOG_(WARN, "fail to assign default region name");
  }
  return ret;
  #undef PRINT_WRAPPER
}

int TableAccessor::is_primary_region(const ObStringHolder &region_name_holder, bool &is_primary_region)
{
  int ret = OB_SUCCESS;
  is_primary_region = true;
  return ret;
}

int TableAccessor::calculate_zone_priority_score(ObStringHolder &zone_priority, ObStringHolder &self_zone_name, int64_t &score)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(zone_priority), K(score), K(self_zone_name)
  int ret = OB_SUCCESS;
  ObArray<ObStringHolder> split_by_semicolon;
  score = INT64_MAX;
  if (zone_priority.empty()) {
    COORDINATOR_LOG_(WARN, "zone_priority is empty");
  } else if (CLICK_FAIL(ObTableAccessHelper::split_string_by_char(zone_priority, ';', split_by_semicolon))) {
    COORDINATOR_LOG_(WARN, "split zone priority by semicolon failed");
  } else {
    for (int64_t idx = 0; idx < split_by_semicolon.count() && OB_SUCC(ret) && score == INT64_MAX; ++idx)
    {
      ObArray<ObStringHolder> split_by_comma;
      if (CLICK_FAIL(ObTableAccessHelper::split_string_by_char(split_by_semicolon[idx], ',', split_by_comma))) {
        COORDINATOR_LOG_(WARN, "split string by comma failed", K(split_by_semicolon));
      } else {
        for (int64_t idx2 = 0; idx2 < split_by_comma.count() && score == INT64_MAX; ++idx2) {
          if (split_by_comma[idx2].get_ob_string().case_compare(self_zone_name.get_ob_string()) == 0) {
            score = idx;
            COORDINATOR_LOG_(TRACE, "get zone priority score success");
          }
        }
      }
    }
    if (score == INT64_MAX) {
      COORDINATOR_LOG_(WARN, "zone name not found in zone priority column");
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int TableAccessor::get_removed_status_and_reason(ObStringHolder &blacklist, bool &status, ObStringHolder &reason)
{
  LC_TIME_GUARD(1_s);
  #define PRINT_WRAPPER KR(ret), K(blacklist), K(status), K(reason)
  int ret = OB_SUCCESS;
  ObArray<ObStringHolder> arr;
  if (!blacklist.empty() && CLICK_FAIL(ObTableAccessHelper::split_string_by_char(blacklist, ';', arr))) {
    COORDINATOR_LOG_(WARN, "split_string_by_char(;) failed");
  } else {
    char ip_port_string[MAX_IP_PORT_LENGTH] = {0};
    if (CLICK_FAIL(GCTX.self_addr().ip_port_to_string(ip_port_string, MAX_IP_PORT_LENGTH))) {
      COORDINATOR_LOG_(WARN, "self addr to string failed");
    } else {
      int64_t len = strlen(ip_port_string);
      status = false;
      for (int64_t idx = 0; idx < arr.count() && OB_SUCC(ret) && !status; ++idx) {
        if (memcmp(arr[idx].get_ob_string().ptr(), ip_port_string, len) == 0) {
          ObString str = arr[idx].get_ob_string();
          const char *pos1 = str.find('(');
          const char *pos2 = str.reverse_find(')');
          if (pos1 == nullptr || pos2 == nullptr || pos2 - pos1 < 1) {
            ret = OB_ERR_UNEXPECTED;
            COORDINATOR_LOG_(WARN, "find '(' and ')' failed", K(str));
          } else {
            ObString temp_reason(pos2 - pos1, pos1 + 1);
            if (CLICK_FAIL(reason.assign(temp_reason))) {
              COORDINATOR_LOG_(WARN, "create reason ObStringHolder failed", K(str));
            } else {
              status = true;
            }
          }
        }
      }
    }
  }
  return ret;
  #undef PRINT_WRAPPER
}

int TableAccessor::get_zone_stop_status(ObStringHolder &zone_name, bool &is_zone_stopped)
{
  int ret = OB_SUCCESS;
  is_zone_stopped = false;
  return ret;
}

int TableAccessor::get_server_stop_status(bool &is_server_stopped)
{
  LC_TIME_GUARD(1_s);
  int ret = OB_SUCCESS;
  is_server_stopped = false;
  return ret;
}

}
}
}
