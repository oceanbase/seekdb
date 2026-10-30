#include <gtest/gtest.h>
#include "observer/ob_inner_sql_result.h"
#include "sql/session/ob_sql_session_info.h"
#include "query/plan_cache/ob_plan_cache_access_service.h"
#include "common/datum/datum_parameter_codec.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
using namespace oceanbase::common;
using namespace oceanbase::observer;
using namespace oceanbase::sql;
namespace {
class NoopPlanAccess final : public oceanbase::query::ObIPlanCacheAccessService {
 public: void enter_access() override {} void leave_access() override {}
 void check_current_thread() override {} int get_global_safe_timestamp(int64_t &v) const override {v=0;return 0;}
};
struct OpenedTag { using type=bool ObInnerSQLResult::*; friend type access(OpenedTag); };
struct RowTag { using type=const ObNewRow *ObInnerSQLResult::*; friend type access(RowTag); };
template<class T,typename T::type M>struct Access { friend typename T::type access(T){return M;} };
template struct Access<OpenedTag,&ObInnerSQLResult::opened_>;
template struct Access<RowTag,&ObInnerSQLResult::row_>;
static std::string hex(const char *p,int64_t n){const char*t="0123456789abcdef";std::string r;for(int64_t i=0;i<n;++i){unsigned char c=p[i];r+=t[c>>4];r+=t[c&15];}return r;}
static std::string wire(const DatumValue &value)
{
  int64_t size = 0;
  const int error = DatumParameterCodec::value_size(value.datum_, value.meta_, size);
  if (error != OB_SUCCESS) { return "ERR" + std::to_string(error); }
  std::string bytes(size, '\0');
  int64_t pos = 0;
  const int ret = DatumParameterCodec::encode_value(value.datum_, value.meta_, bytes.data(), bytes.size(), pos);
  return ret == OB_SUCCESS ? hex(bytes.data(), pos) : "ERR" + std::to_string(ret);
}
template<class T>static std::string bits(const T&v){return hex(reinterpret_cast<const char*>(&v),sizeof v);}
static void emit(const char*scenario,int64_t index,const char*method,int r,const std::string &output){std::cout<<scenario<<'\t'<<index<<'\t'<<method<<'\t'<<r<<'\t'<<output<<'\n';}
static void check(const char*name,ObInnerSQLResult&r,int64_t i,bool timestamp){
 int64_t vi=99;int e=r.get_int(i,vi);emit(name,i,"int",e,bits(vi));uint64_t vu=99;e=r.get_uint(i,vu);emit(name,i,"uint",e,bits(vu));
 bool vb=true;e=r.get_bool(i,vb);emit(name,i,"bool",e,bits(vb));float vf=99;e=r.get_float(i,vf);emit(name,i,"float",e,bits(vf));double vd=99;e=r.get_double(i,vd);emit(name,i,"double",e,bits(vd));
 int64_t dt=99;e=r.get_datetime(i,dt);emit(name,i,"datetime",e,bits(dt));int32_t da=99;e=r.get_date(i,da);emit(name,i,"date",e,bits(da));int64_t tm=99;e=r.get_time(i,tm);emit(name,i,"time",e,bits(tm));uint8_t yr=99;e=r.get_year(i,yr);emit(name,i,"year",e,bits(yr));
 if(timestamp){int64_t ts=99;e=r.get_timestamp(i,nullptr,ts);emit(name,i,"timestamp",e,bits(ts));}
 ObString string=ObString::make_string("unchanged");e=r.get_varchar(i,string);emit(name,i,"varchar",e,hex(string.ptr(),string.length()));
 SqlTypeMeta meta;meta.set_double();e=r.get_type(i,meta);emit(name,i,"type",e,hex(reinterpret_cast<const char*>(&meta),sizeof meta));
 ObArenaAllocator output_owner;DatumValue out;out.set_int(99, output_owner);e=r.get_value(i,out);emit(name,i,"value",e,wire(out));
}
struct NativeFlagTag { using type=bool ObInnerSQLResult::*; friend type access(NativeFlagTag); };
struct NativeViewTag { using type=ResultRowView ObInnerSQLResult::*; friend type access(NativeViewTag); };
template struct Access<NativeFlagTag,&ObInnerSQLResult::native_result_>;
template struct Access<NativeViewTag,&ObInnerSQLResult::native_row_>;
class BoundResult {
public:
  BoundResult() : access_service_(), session_(), result_(session_, access_service_, false) {}
  ~BoundResult() { result_.*access(OpenedTag{}) = false; }
  void bind(const ObNewRow *row, bool view = false) {
    result_.*access(OpenedTag{}) = true;
    result_.*access(RowTag{}) = row;
    result_.*access(NativeFlagTag{}) = view;
    ResultRowView native; native.set_legacy(row);
    result_.*access(NativeViewTag{}) = native;
  }
  NoopPlanAccess access_service_;
  ObSQLSessionInfo session_;
  ObInnerSQLResult result_;
};
class CoutGuard {
public:
  explicit CoutGuard(std::ostream &output) : previous_(std::cout.rdbuf(output.rdbuf())) {}
  ~CoutGuard() { std::cout.rdbuf(previous_); }
private: std::streambuf *previous_;
};
class FailOwner final : public ObIAllocator {
public:
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
std::vector<std::string> fields(const std::string &line) {
  std::vector<std::string> result;
  for (size_t begin = 0; ; ) {
    const size_t end = line.find('\t', begin);
    result.push_back(line.substr(begin, end - begin));
    if (end == std::string::npos) { break; }
    begin = end + 1;
  }
  return result;
}
std::string unhex(const std::string &text) {
  const char *digits = "0123456789abcdef";
  std::string bytes;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *hi = std::strchr(digits, text[i]), *lo = std::strchr(digits, text[i + 1]);
    if (hi == nullptr || lo == nullptr) { ADD_FAILURE() << "invalid golden hex"; return {}; }
    bytes.push_back(static_cast<char>((hi - digits) * 16 + lo - digits));
  }
  return bytes;
}
}
TEST(NativeInnerResult, ActualOldGettersAndProjectedNativeRows)
{
  std::ifstream input("unittest/observer/data/native_inner_result.golden");
  ASSERT_TRUE(input.good());
  std::ostringstream expected; expected << input.rdbuf();
  std::vector<std::pair<std::string, DatumValue>> values;
  ObArenaAllocator owner;
  std::istringstream lines(expected.str());
  std::string line;
  while (std::getline(lines, line)) {
    const auto parts = fields(line);
    if (parts.size() == 3 && parts[0] == "INPUT") {
      const std::string bytes = unhex(parts[2]);
      DatumValue value;
      int64_t pos = 0;
      ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::decode_value(bytes.data(), bytes.size(), pos,
          owner, value.datum_, value.meta_));
      ASSERT_EQ(bytes.size(), pos);
      values.push_back({parts[1], value});
    }
  }
  ASSERT_EQ(20, values.size());
  std::ostringstream actual;
  {
    CoutGuard output(actual);
    BoundResult bound;
    auto &result = bound.result_;
    for (auto &entry : values) {
      ObNewRow row; row.cells_ = &entry.second; row.count_ = 1;
      bound.bind(&row);
      std::cout << "INPUT\t" << entry.first << '\t' << wire(entry.second) << '\n';
      const auto map = ObDatum::get_sql_value_datum_map_type(entry.second.get_type());
      check(entry.first.c_str(), result, 0,
          map == OBJ_DATUM_8BYTE_DATA || map == OBJ_DATUM_4BYTE_DATA || map == OBJ_DATUM_1BYTE_DATA
          || entry.second.is_null() || entry.second.is_min_value() || entry.second.is_max_value());
    }
    DatumValue cells[3];
    EXPECT_EQ(OB_SUCCESS, cells[0].set_int(11, owner));
    EXPECT_EQ(OB_SUCCESS, cells[1].set_int(22, owner));
    EXPECT_EQ(OB_SUCCESS, cells[2].set_int(33, owner));
    int32_t projector[3] = {2, 0, 2};
    ObNewRow row; row.cells_ = cells; row.count_ = 3; row.projector_ = projector; row.projector_size_ = 3;
    bound.bind(&row);
    for (int64_t i = 0; i < 4; ++i) { check("projector", result, i, true); }
    projector[0] = -1; projector[1] = 3;
    for (int64_t i = 0; i < 2; ++i) { check("invalid_projector", result, i, true); }
    bound.bind(nullptr); check("no_row", result, 0, true);
    result.*access(OpenedTag{}) = false; check("closed", result, 0, true);
  }
  EXPECT_EQ(expected.str(), actual.str());
}
TEST(NativeInnerResult, NativeViewBorrowAndExplicitOwnerClone)
{
  ObArenaAllocator source_owner, result_owner;
  DatumValue source;
  const std::string text(256, 's');
  ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString(text.size(), text.data()), source_owner));
  source.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  source.set_scale(7);
  ObNewRow row; row.cells_ = &source; row.count_ = 1;
  BoundResult bound; bound.bind(&row, true);
  DatumValue borrowed, owned;
  ASSERT_EQ(OB_SUCCESS, bound.result_.get_value(int64_t(0), borrowed));
  ASSERT_EQ(source.datum_.ptr_, borrowed.datum_.ptr_);
  ASSERT_EQ(OB_SUCCESS, bound.result_.get_native_value(0, result_owner, owned));
  ASSERT_NE(source.datum_.ptr_, owned.datum_.ptr_);
  ASSERT_EQ(source.meta_, owned.meta_);
  std::memset(const_cast<char *>(source.datum_.ptr_), 'x', source.datum_.len_);
  EXPECT_EQ('x', borrowed.get_string().ptr()[0]);
  EXPECT_EQ(text, std::string(owned.get_string().ptr(), owned.get_string().length()));
  source_owner.reuse();
  EXPECT_EQ(text, std::string(owned.get_string().ptr(), owned.get_string().length()));
}
TEST(NativeInnerResult, FailureDoesNotPublishAndNegativeIndexIsRejected)
{
  ObArenaAllocator owner;
  DatumValue source, output;
  ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString::make_string("source bytes"), owner));
  source.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, output.set_int(99, owner));
  const std::string previous = wire(output);
  ObNewRow row; row.cells_ = &source; row.count_ = 1;
  BoundResult bound; bound.bind(&row, true);
  FailOwner fail;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, bound.result_.get_native_value(0, fail, output));
  EXPECT_EQ(previous, wire(output));
  EXPECT_EQ(OB_INVALID_ARGUMENT, bound.result_.get_value(-1, output));
  EXPECT_EQ(previous, wire(output));
  EXPECT_EQ(OB_INVALID_ARGUMENT, bound.result_.get_value(static_cast<const char *>(nullptr), output));
  EXPECT_EQ(previous, wire(output));
}
