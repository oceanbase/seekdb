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

#include <gtest/gtest.h>
#include <mutex>

#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/string/ob_string.h"
#include "observer/mysql/obmp_utils.h"
#include "rpc/obmysql/packet/ompk_ok.h"
#include "share/ob_define.h"
#include "share/system_variable/ob_sys_var_class_type.h"
#include "share/system_variable/ob_system_variable_factory.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;
using namespace oceanbase::observer;
using namespace oceanbase::share;
using namespace oceanbase::sql;

static bool find_sys_var_value(const ObIArray<ObStringKV> &vars, const ObString &key, ObString &value);

namespace
{
std::once_flag g_test_init_flag;
int g_test_init_ret = OB_SUCCESS;

int init_test_env()
{
  std::call_once(g_test_init_flag, []() {
    g_test_init_ret = ObSysVariables::init_default_values();
    if (OB_SUCCESS == g_test_init_ret) {
      g_test_init_ret = ObCharset::init_charset();
    }
  });
  return g_test_init_ret;
}

bool is_digits_only(const ObString &str)
{
  bool ret = !str.empty();
  for (int64_t i = 0; ret && i < str.length(); ++i) {
    const char c = str.ptr()[i];
    ret = (c >= '0' && c <= '9');
  }
  return ret;
}

void init_session(ObArenaAllocator &allocator, ObSQLSessionInfo &session)
{
  ASSERT_EQ(OB_SUCCESS, session.init(1 /*sessid*/, 0 /*proxy_sessid*/, &allocator));
  {
    oceanbase::obmysql::ObMySQLCapabilityFlags cap;
    cap.capability_ = 0;
    cap.cap_flags_.OB_CLIENT_SESSION_TRACK = 1;
    session.set_capability(cap);
  }
  ASSERT_EQ(OB_SUCCESS, init_test_env());
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false /*print_info_log*/, true /*is_sys_tenant*/));
  session.reset_session_changed_info();
}

void assert_sys_var_show_str_eq(const ObIArray<ObStringKV> &vars,
                                const ObSysVarClassType sys_var_id,
                                const ObString &expected)
{
  ObString actual;
  ASSERT_TRUE(find_sys_var_value(vars, ObSysVarFactory::get_sys_var_name_by_id(sys_var_id), actual));
  ASSERT_EQ(expected, actual);
  ASSERT_FALSE(is_digits_only(actual));
}
} // namespace

static bool find_sys_var_value(const ObIArray<ObStringKV> &vars, const ObString &key, ObString &value)
{
  bool found = false;
  for (int64_t i = 0; !found && i < vars.count(); ++i) {
    if (vars.at(i).key_ == key) {
      value = vars.at(i).value_;
      found = true;
    }
  }
  return found;
}

TEST(TestSessionTrackSysVar, charset_and_collation_show_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObSQLSessionInfo session;
  init_session(allocator, session);

  const int64_t target_collation_id = CS_TYPE_UTF8MB4_0900_AI_CI; // 255
  const int64_t fallback_collation_id = CS_TYPE_UTF8MB4_GENERAL_CI; // 45

  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_COLLATION_CONNECTION, fallback_collation_id));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_CHARACTER_SET_RESULTS, fallback_collation_id));
  session.reset_session_changed_info();
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_COLLATION_CONNECTION, target_collation_id));
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_CHARACTER_SET_RESULTS, target_collation_id));
  ASSERT_TRUE(session.is_sys_var_changed());

  const ObString expected_collation =
      ObString::make_string(ObCharset::collation_name(static_cast<ObCollationType>(target_collation_id)));
  const ObCharsetType expected_charset_type =
      ObCharset::charset_type_by_coll(static_cast<ObCollationType>(target_collation_id));
  const ObString expected_charset = ObString::make_string(ObCharset::charset_name(expected_charset_type));

  {
    OMPKOK ok_pkt;
    ASSERT_EQ(OB_SUCCESS, ObMPUtils::add_changed_session_info(ok_pkt, session));
    const ObIArray<ObStringKV> &vars = ok_pkt.get_system_vars();
    assert_sys_var_show_str_eq(vars, SYS_VAR_COLLATION_CONNECTION, expected_collation);
    assert_sys_var_show_str_eq(vars, SYS_VAR_CHARACTER_SET_RESULTS, expected_charset);
  }

  {
    OMPKOK ok_pkt;
    ASSERT_EQ(OB_SUCCESS, ObMPUtils::add_session_info_on_connect(ok_pkt, session));
    const ObIArray<ObStringKV> &vars = ok_pkt.get_system_vars();
    assert_sys_var_show_str_eq(vars, SYS_VAR_COLLATION_CONNECTION, expected_collation);
    assert_sys_var_show_str_eq(vars, SYS_VAR_CHARACTER_SET_RESULTS, expected_charset);
  }
}

TEST(TestSessionTrackSysVar, sql_mode_show_string)
{
  ObArenaAllocator allocator(ObModIds::TEST);
  ObSQLSessionInfo session;
  init_session(allocator, session);

  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_SQL_MODE, 0));
  session.reset_session_changed_info();

  const ObString expected_sql_mode = ObString::make_string("ANSI_QUOTES");
  ASSERT_EQ(OB_SUCCESS, session.update_sys_variable(SYS_VAR_SQL_MODE, static_cast<int64_t>(SMO_ANSI_QUOTES)));
  ASSERT_TRUE(session.is_sys_var_changed());

  {
    OMPKOK ok_pkt;
    ASSERT_EQ(OB_SUCCESS, ObMPUtils::add_changed_session_info(ok_pkt, session));
    const ObIArray<ObStringKV> &vars = ok_pkt.get_system_vars();
    assert_sys_var_show_str_eq(vars, SYS_VAR_SQL_MODE, expected_sql_mode);
  }

  {
    OMPKOK ok_pkt;
    ASSERT_EQ(OB_SUCCESS, ObMPUtils::add_session_info_on_connect(ok_pkt, session));
    const ObIArray<ObStringKV> &vars = ok_pkt.get_system_vars();
    assert_sys_var_show_str_eq(vars, SYS_VAR_SQL_MODE, expected_sql_mode);
  }
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
