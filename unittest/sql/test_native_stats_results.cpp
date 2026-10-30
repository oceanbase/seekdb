/* Compare genuine native production methods with immutable old production outputs. */
#include "sql/optimizer/stat/ob_dbms_stats_preferences.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
#include "sql/optimizer/stat/ob_min_max_estimator.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/typed_datum_compare.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>
using namespace oceanbase;using namespace common;
namespace {
std::vector<std::string> split(const std::string&line){std::vector<std::string>r;std::istringstream in(line);std::string s;while(std::getline(in,s,'\t'))r.push_back(s);return r;}
std::string unhex(const std::string&s){std::string out;if(s=="-")return out;for(size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16));return out;}
std::string fixture(){const char*env=getenv("TEST_SRCDIR");if(env){std::string p=std::string(env)+"/_main/unittest/sql/data/native_stats_results.golden";std::ifstream in(p);if(in)return p;}return "unittest/sql/data/native_stats_results.golden";}
std::string outputs(const ObTableStatParam&p){std::ostringstream out;out<<std::setprecision(17)<<p.degree_<<'\t'<<p.sample_info_.sample_value_<<'\t'<<p.is_auto_sample_size_<<'\t'<<p.hist_sample_info_.sample_value_<<'\t'<<p.async_full_table_size_<<'\t'<<p.auto_sample_row_cnt_<<'\t'<<p.online_sample_percent_;return out.str();}
template<class T>void check_pref(const std::vector<std::string>&f){ObArenaAllocator owner;std::string text=unhex(f[2]);T checker(&owner,nullptr,ObString(text.size(),text.data()));ObTableStatParam param;ASSERT_EQ(std::stoi(f[3]),checker.check_pref_value_validity(&param));std::string expected;for(size_t i=4;i<f.size();++i){if(i!=4)expected+='\t';expected+=f[i];}EXPECT_EQ(expected,outputs(param));}
class FailOwner final:public ObIAllocator {public:void*alloc(int64_t)override{return nullptr;}void*alloc(int64_t,const ObMemAttr&)override{return nullptr;}void free(void*)override{}};
}
TEST(NativeStatsResults, actual_old_numeric_preference_errors_and_results){std::ifstream in(fixture());ASSERT_TRUE(in.good());std::string line;int count=0;while(std::getline(in,line)){auto f=split(line);if(f[0]!="PREF")continue;++count;SCOPED_TRACE(line);
#define PREF(C) if(f[1]==#C){check_pref<C>(f);}else
 PREF(ObDegreePrefs) PREF(ObEstimatePercentPrefs) PREF(ObStalePercentPrefs) PREF(ObAsyncGatherStaleRatioPrefs) PREF(ObAsyncGatherSampleSizePrefs) PREF(ObAsyncGatherFullTableSizePrefs) PREF(ObAsyncStaleMaxTableSizePrefs) PREF(ObHistEstPercentPrefs) PREF(ObGatherStatBatchSizePrefs) PREF(ObAutoSampleRowCountPrefs) PREF(ObOnlineEstimatePercentPrefs) {FAIL()<<f[1];}
#undef PREF
 }EXPECT_EQ(308,count);}
TEST(NativeStatsResults, actual_old_integer_and_double_casts){std::ifstream in(fixture());ASSERT_TRUE(in.good());std::string line;int count=0;ObStatItem item;while(std::getline(in,line)){auto f=split(line);if(f[0]!="INT"&&f[0]!="NUM")continue;++count;SCOPED_TRACE(line);ObArenaAllocator owner;std::string bytes=unhex(f[1]);DatumValue v;int64_t pos=0;ASSERT_EQ(0,DatumParameterCodec::decode_value(bytes.data(),bytes.size(),pos,owner,v.datum_,v.meta_));ASSERT_EQ(bytes.size(),pos);if(f[0]=="INT"){int64_t out=77;EXPECT_EQ(std::stoi(f[2]),item.cast_int(v,out));EXPECT_EQ(std::stoll(f[3]),out);}else{double out=77;EXPECT_EQ(std::stoi(f[2]),ObDbmsStatsUtils::cast_number_to_double(v.get_number(),out));uint64_t bits;std::memcpy(&bits,&out,8);EXPECT_EQ(std::stoull(f[3]),bits);}}EXPECT_EQ(49,count);}
TEST(NativeStatsResults, native_read_value_owning_clone_survives_next_and_failure){ObArenaAllocator cursor_owner,retained_owner;DatumValue cursor,retained;ASSERT_EQ(0,cursor.set_varchar(ObString::make_string("retained preference"),cursor_owner));ASSERT_EQ(0,clone_typed_value(cursor,retained,retained_owner));cursor_owner.reset();EXPECT_EQ(ObString::make_string("retained preference"),retained.get_string());FailOwner failing;EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,clone_typed_value(retained,retained,failing));EXPECT_EQ(ObString::make_string("retained preference"),retained.get_string());}

TEST(NativeStatsResults, decoded_preference_owns_cursor_text_and_oom_does_not_publish){
 ObArenaAllocator cursor_owner,pref_owner;DatumValue name,value;
 ASSERT_EQ(0,name.set_varchar(ObString::make_string("DEGREE"),cursor_owner));
 ASSERT_EQ(0,value.set_varchar(ObString::make_string("16"),cursor_owner));
 ObDegreePrefs pref(&pref_owner,nullptr,ObString::make_string("old"));ObTableStatParam param;
 ASSERT_EQ(0,pref.decode_pref_result(&pref_owner,name,value,param));EXPECT_EQ(16,param.degree_);
 cursor_owner.reset();ObString out_name,out_value;
 ASSERT_EQ(0,pref.dump_pref_name_and_value(out_name,out_value));EXPECT_EQ(ObString::make_string("16"),out_value);
 ASSERT_EQ(0,name.set_varchar(ObString::make_string("DEGREE"),cursor_owner));
 ASSERT_EQ(0,value.set_varchar(ObString::make_string("24"),cursor_owner));
 ObDegreePrefs unchanged(&pref_owner,nullptr,ObString::make_string("old"));FailOwner failing;
 EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,unchanged.decode_pref_result(&failing,name,value,param));
 EXPECT_FALSE(unchanged.is_decoded());ASSERT_EQ(0,unchanged.dump_pref_name_and_value(out_name,out_value));EXPECT_EQ(ObString::make_string("old"),out_value);
}

namespace {
class ToggleStatOwner final : public ObIAllocator {
public:
  void *alloc(int64_t bytes) override { return reject ? nullptr : arena.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *) override {}
  bool reject = false;
  ObArenaAllocator arena;
};
}

TEST(NativeStatsResults, minmax_subquery_virtual_decode_accepts_null)
{
  for (bool is_min : {true, false}) {
    SCOPED_TRACE(is_min);
    ObArenaAllocator source_owner, scratch;
    ObOptColumnStat stat;
    DatumValue previous;
    ASSERT_EQ(OB_SUCCESS, previous.set_int(42, source_owner));
    ASSERT_EQ(OB_SUCCESS, stat.set_min_value(previous));
    ASSERT_EQ(OB_SUCCESS, stat.set_max_value(previous));
    ObColumnStatParam column;
    ObString empty;
    ObStatMinMaxSubquery decoder(&column, &stat, empty, empty, empty, is_min);
    ObStatItem &dispatch = decoder;
    DatumValue null_value;
    null_value.meta_.set_varchar();
    ASSERT_EQ(OB_SUCCESS, dispatch.decode(null_value, scratch, nullptr));
    EXPECT_TRUE((is_min ? stat.get_min_value() : stat.get_max_value()).is_null());
    EXPECT_EQ(42, (is_min ? stat.get_max_value() : stat.get_min_value()).get_int());
  }
}

TEST(NativeStatsResults, minmax_subquery_utf8_prefix_is_owned_by_stat)
{
  for (bool is_min : {true, false}) {
    SCOPED_TRACE(is_min);
    ObArenaAllocator source_owner, scratch;
    ObOptColumnStat stat;
    std::string text;
    for (int64_t i = 0; i < OPT_STATS_MAX_VALUE_CHAR_LEN + 12; ++i) {
      text += "\xe4\xb8\xad";
    }
    const std::string expected = text.substr(0, OPT_STATS_MAX_VALUE_CHAR_LEN * 3);
    DatumValue source;
    ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString(text.size(), text.data()), source_owner));
    source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    source.set_collation_level(CS_LEVEL_COERCIBLE);
    ObColumnStatParam column;
    ObString empty;
    ObStatMinMaxSubquery decoder(&column, &stat, empty, empty, empty, is_min);
    ObStatItem &dispatch = decoder;
    ASSERT_EQ(OB_SUCCESS, dispatch.decode(source, scratch, nullptr));
    const DatumValue &result = is_min ? stat.get_min_value() : stat.get_max_value();
    ASSERT_EQ(OPT_STATS_MAX_VALUE_CHAR_LEN * 3, source.get_string_len());
    ASSERT_EQ(OPT_STATS_MAX_VALUE_CHAR_LEN * 3, result.get_string_len());
    ASSERT_NE(source.get_string_ptr(), result.get_string_ptr());
    EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, result.get_collation_type());
    EXPECT_EQ(CS_LEVEL_COERCIBLE, result.get_collation_level());
    std::memset(const_cast<char *>(source.get_string_ptr()), 'x', source.get_string_len());
    source_owner.reset();
    EXPECT_EQ(ObString(expected.size(), const_cast<char *>(expected.data())), result.get_string());
  }
}

TEST(NativeStatsResults, minmax_subquery_clone_oom_keeps_previous_bound)
{
  for (bool is_min : {true, false}) {
    SCOPED_TRACE(is_min);
    ToggleStatOwner stat_owner;
    ObArenaAllocator source_owner, scratch;
    ObOptColumnStat stat(stat_owner);
    DatumValue previous, source;
    ASSERT_EQ(OB_SUCCESS, previous.set_varchar(ObString::make_string("previous bound"), source_owner));
    ASSERT_EQ(OB_SUCCESS, stat.set_min_value(previous));
    ASSERT_EQ(OB_SUCCESS, stat.set_max_value(previous));
    ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString::make_string("replacement bound"), source_owner));
    source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    const char *old_bytes = (is_min ? stat.get_min_value() : stat.get_max_value()).get_string_ptr();
    ObColumnStatParam column;
    ObString empty;
    ObStatMinMaxSubquery decoder(&column, &stat, empty, empty, empty, is_min);
    ObStatItem &dispatch = decoder;
    stat_owner.reject = true;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, dispatch.decode(source, scratch, nullptr));
    const DatumValue &result = is_min ? stat.get_min_value() : stat.get_max_value();
    EXPECT_EQ(old_bytes, result.get_string_ptr());
    EXPECT_EQ(ObString::make_string("previous bound"), result.get_string());
  }
}
