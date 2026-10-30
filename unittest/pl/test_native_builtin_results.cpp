/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "pl/ob_pl_interface_pragma.h"
#include "pl/ob_pl.h"
#include "common/datum/datum_value.h"
#include "common/datum/datum_parameter_codec.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <set>
#include <type_traits>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;
namespace {
std::vector<std::string> old_fields(char kind)
{
  std::ifstream input("unittest/pl/data/native_builtin_results.golden");
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line[0] == kind) {
      std::istringstream columns(line); std::string value; std::vector<std::string> fields;
      while (std::getline(columns, value, '\t')) { fields.push_back(value); }
      return fields;
    }
  }
  return {};
}
std::string value_wire(const DatumValue &value)
{
  char data[128]; int64_t pos = 0;
  if (DatumParameterCodec::encode_value(value.datum_, value.meta_, data, sizeof(data), pos) != OB_SUCCESS) { return "encode error"; }
  const char *hex = "0123456789abcdef"; std::string result;
  for (int64_t i = 0; i < pos; ++i) { unsigned char byte = data[i]; result += hex[byte >> 4]; result += hex[byte & 15]; }
  return result;
}
class ToggleAllocator final : public ObIAllocator {
public:
  ToggleAllocator() : fail_(false), live_() {}
  ~ToggleAllocator() { for (void *p : live_) { std::free(p); } }
  void *alloc(int64_t size) override {
    if (fail_) { return nullptr; }
    void *p = std::malloc(size); if (p != nullptr) { live_.insert(p); } return p;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *p) override {
    if (p != nullptr && live_.erase(p) != 0) { std::free(p); }
  }
  bool fail_; std::set<void *> live_;
};
static_assert(std::is_same<PL_C_INTERFACE_t,
    int(*)(ObPLExecCtx &, ParamStore &, DatumValue &)>::value,
    "Every registered PL C result is a native DatumValue");
}
TEST(NativeBuiltinResults, registry_dispatch_preserves_int32_type_and_owned_result)
{
  ToggleAllocator owner;
  ObExecContext context(owner);
  ParamStore params;
  DatumParameter destination;
  int status = OB_SUCCESS;
  ObPLExecCtx pl_context(&owner, &context, &params, &destination, &status, nullptr);
  ObPLInterfaceService registry;
  ASSERT_EQ(OB_SUCCESS, registry.init());
  ObString name = ObString::make_string("TEST");
  PL_C_INTERFACE_t callback = registry.get_entry(name);
  ASSERT_NE(nullptr, callback);
  DatumValue result; result.meta_.set_type(ObMaxType);
  ASSERT_EQ(OB_SUCCESS, callback(pl_context, params, result));
  EXPECT_EQ(ObInt32Type, result.get_type());
  EXPECT_EQ(42, result.get_int());
  EXPECT_EQ(sizeof(int64_t), result.datum_.len_);
  const auto old = old_fields('R'); ASSERT_EQ(6, old.size());
  EXPECT_EQ(std::stoi(old[3]), int(result.get_type()));
  EXPECT_EQ(std::stoll(old[4]), result.get_int());
  EXPECT_EQ(old[5], value_wire(result));
}
TEST(NativeBuiltinResults, callback_oom_does_not_publish_a_partial_result)
{
  ToggleAllocator owner;
  ObExecContext context(owner);
  ParamStore params;
  DatumValue result;
  ASSERT_EQ(OB_SUCCESS, result.set_int(37, owner));
  const char *previous = result.datum_.ptr_;
  owner.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, ObPLInterfaceImpl::call(context, params, result));
  EXPECT_EQ(ObIntType, result.get_type());
  EXPECT_EQ(previous, result.datum_.ptr_);
  EXPECT_EQ(37, result.get_int());
}
TEST(NativeBuiltinResults, unsupported_procedure_keeps_no_return_distinct_from_sql_null)
{
  ToggleAllocator owner;
  ObExecContext context(owner);
  ParamStore params;
  DatumParameter destination;
  int status = OB_SUCCESS;
  ObPLExecCtx pl_context(&owner, &context, &params, &destination, &status, nullptr);
  ObPLInterfaceService registry;
  ASSERT_EQ(OB_SUCCESS, registry.init());
  ObString name = ObString::make_string("OB_SESSION_TRACE_ENABLE");
  PL_C_INTERFACE_t callback = registry.get_entry(name);
  ASSERT_NE(nullptr, callback);
  DatumValue result; result.meta_.set_type(ObMaxType);
  EXPECT_EQ(OB_NOT_SUPPORTED, callback(pl_context, params, result));
  EXPECT_EQ(ObMaxType, result.get_type());
  const auto old = old_fields('P'); ASSERT_EQ(4, old.size());
  EXPECT_EQ(OB_NOT_SUPPORTED, std::stoi(old[2]));
  EXPECT_EQ(std::stoi(old[3]), int(result.get_type()));
  DatumValue sql_null;
  EXPECT_EQ(ObNullType, sql_null.get_type());
}
int main(int argc, char **argv)
{
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
