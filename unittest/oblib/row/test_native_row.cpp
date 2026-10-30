/* Native row tests compare actual immutable old production outputs. */
#include "common/row/ob_row.h"
#include "common/datum/datum_parameter_array.h"
#include "lib/charset/ob_charset.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native rows must not include the legacy value definition"
#endif
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cstring>
using namespace oceanbase::common;
namespace {
std::vector<std::string> fields(const std::string &line) {
 std::vector<std::string> result;std::istringstream in(line);std::string cell;
 while(std::getline(in,cell,'\t'))result.push_back(cell);return result;
}
std::string unhex(const std::string &s) {
 if(s=="-")return {};std::string bytes;
 for(size_t i=0;i+1<s.size();i+=2)bytes+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16));return bytes;
}
std::string hex(const char *p,int64_t n) {
 const char *digits="0123456789abcdef";std::string s;
 for(int64_t i=0;i<n;++i){unsigned char c=p[i];s+=digits[c>>4];s+=digits[c&15];}return s.empty()?"-":s;
}
std::string wire(const ObNewRow &row) {
 std::string bytes(row.get_serialize_size()+16,'\0');int64_t pos=0;
 int ret=row.serialize(bytes.data(),bytes.size(),pos);return ret?"ERROR"+std::to_string(ret):hex(bytes.data(),pos);
}
std::string fixture_path() {
 const char *env=getenv("TEST_SRCDIR");if(env){std::string path=std::string(env)+"/_main/unittest/oblib/row/data/native_row.golden";std::ifstream f(path);if(f)return path;}
 return "unittest/oblib/row/data/native_row.golden";
}
class FailOwner final:public ObIAllocator {
public:
 void *alloc(int64_t)override{return nullptr;}void *alloc(int64_t,const ObMemAttr &)override{return nullptr;}void free(void *)override{}
};
}
TEST(NativeRow, actual_old_wire_projectors_and_compact_relocation) {
 std::ifstream in(fixture_path());ASSERT_TRUE(in.good());std::string line;int records=0;
 while(std::getline(in,line)){
  auto f=fields(line);if(f[0]!="WIRE")continue;++records;SCOPED_TRACE(f[1]);
  const int64_t count=std::stoll(f[2]),pc=std::stoll(f[3]);std::string bytes=unhex(f[4]);
  ObArenaAllocator owner;DatumValueBuffer buffer(owner);ASSERT_EQ(0,buffer.reserve(count));std::vector<int32_t> projector(pc);
  ObNewRow row(buffer.data(),count);row.projector_=projector.data();row.projector_size_=pc;int64_t pos=0;
  ASSERT_EQ(0,row.deserialize(owner,bytes.data(),bytes.size(),pos));ASSERT_EQ(bytes.size(),pos);ASSERT_EQ(f[4],wire(row));
  // The decode owner owns every payload, including scalars; wire is no longer borrowed.
  std::fill(bytes.begin(),bytes.end(),char(0xa5));ASSERT_EQ(f[4],wire(row));
  const int64_t size=sizeof(ObNewRow)+row.get_deep_copy_size();std::vector<char> source(size);
  auto *header=new(source.data())ObNewRow;pos=sizeof(ObNewRow);
  ASSERT_EQ(0,header->deep_copy(row,source.data(),size,pos));ASSERT_EQ(size,pos);ASSERT_EQ(f[4],wire(*header));
  std::vector<char> relocated=source;std::fill(source.begin(),source.end(),char(0xcc));ObNewRow *restored=nullptr;pos=0;
  ASSERT_EQ(0,ObNewRow::construct(relocated.data(),relocated.size(),pos,restored));ASSERT_EQ(size,pos);ASSERT_EQ(f[4],wire(*restored));
  ObNewRow copy;ASSERT_EQ(0,ob_write_row(owner,row,copy));ASSERT_EQ(f[4],wire(copy));ASSERT_TRUE(row==copy);
 }
 ASSERT_EQ(20,records);
}
TEST(NativeRow, actual_old_projected_copy) {
 std::ifstream in(fixture_path());ASSERT_TRUE(in.good());std::string line;int records=0;
 while(std::getline(in,line)){
  auto f=fields(line);if(f[0]!="OWN")continue;++records;SCOPED_TRACE(f[1]);ASSERT_EQ("0",f[2]);ASSERT_EQ("0",f[4]);
  std::string bytes=unhex(f[3]);ObArenaAllocator owner;DatumValueBuffer buffer(owner);ASSERT_EQ(0,buffer.reserve(4097));
  std::vector<int32_t> projector(16);ObNewRow row(buffer.data(),4097);row.projector_=projector.data();row.projector_size_=projector.size();int64_t pos=0;
  ASSERT_EQ(0,row.deserialize(owner,bytes.data(),bytes.size(),pos));ObNewRow projected;
  ASSERT_EQ(0,ob_write_row_by_projector(owner,row,projected));ASSERT_EQ(f[5],wire(projected));ASSERT_EQ(0,projected.projector_size_);
 }
 ASSERT_EQ(20,records);
}
TEST(NativeRow, actual_old_legal_unis_omission_and_error_atomicity) {
 std::ifstream in(fixture_path());ASSERT_TRUE(in.good());std::string line;int records=0;
 while(std::getline(in,line)){
  auto f=fields(line);if(f[0]!="REUSE")continue;++records;SCOPED_TRACE(f[1]);
  ObArenaAllocator owner;DatumValueBuffer buffer(owner);const int64_t count=f[1]=="cells_missing"||f[1].find("partial_meta")==0?1:2;
  ASSERT_EQ(0,buffer.reserve(count));for(int64_t i=0;i<count;++i){ASSERT_EQ(0,buffer.set_int(i,77));buffer.at(i).set_scale(7);}
  int32_t projector[2]={0,0};ObNewRow row(f[1]=="cells_missing"?nullptr:buffer.data(),count);
  row.projector_=projector;row.projector_size_=f[1]=="empty"?2:0;int64_t pos=0;std::string bytes=unhex(f[2]);
  const int expected=std::stoi(f[3]);ASSERT_EQ(expected,row.deserialize(owner,bytes.data(),bytes.size(),pos));
  if(expected==0){ASSERT_EQ(std::stoll(f[4]),pos);ASSERT_EQ(f[7],wire(row));}
  else {ASSERT_EQ(0,pos);ASSERT_EQ(count,row.count_);if(row.cells_)ASSERT_EQ(77,row.cells_[0].get_int());}
 }
 ASSERT_EQ(9,records);
}
TEST(NativeRow, actual_old_extended_value_equality_is_distinct_from_key_ordering) {
 std::ifstream in(fixture_path());ASSERT_TRUE(in.good());std::string line;int count=0;ObArenaAllocator owner;
 DatumValue left,right;
 auto make=[&](const std::string &name,DatumValue &value){if(name=="min")value.set_min_value();else if(name=="max")value.set_max_value();else ASSERT_EQ(0,value.set_extend(ObActionFlag::OP_NOP,0,owner));};
 while(std::getline(in,line)){auto f=fields(line);if(f[0]!="EXT_EQUAL")continue;++count;SCOPED_TRACE(f[1]+"/"+f[2]);make(f[1],left);make(f[2],right);ObNewRow a(&left,1),b(&right,1);ASSERT_EQ(std::stoi(f[3])!=0,a==b);}
 ASSERT_EQ(9,count);
}
TEST(NativeRow, one_reserve_owns_small_scalar_slots_and_keeps_large_views_borrowed) {
 ObArenaAllocator owner;DatumValueBuffer values(owner);ASSERT_EQ(0,values.reserve(3));auto *base=values.data();
 for(int i=0;i<10000;++i){ASSERT_EQ(0,values.set_int(0,i));ASSERT_EQ(base,values.data());ASSERT_EQ(i,values.at(0).get_int());}
 char small[]="small",large[]="this string is longer than sixteen bytes";
 ASSERT_EQ(0,values.set_varchar_view(1,ObString::make_string(small)));ASSERT_EQ(0,values.set_varchar_view(2,ObString::make_string(large)));
 small[0]='X';large[0]='X';ASSERT_EQ('s',values.at(1).get_string()[0]);ASSERT_EQ('X',values.at(2).get_string()[0]);
 ASSERT_EQ(0,values.reserve(8));ASSERT_EQ(9999,values.at(0).get_int());ASSERT_EQ('s',values.at(1).get_string()[0]);
 ASSERT_EQ(0,DatumValueBuffer::set_uint32(values.data(),values.capacity(),0,UINT32_MAX));ASSERT_EQ(ObUInt32Type,values.at(0).get_type());ASSERT_EQ(UINT32_MAX,values.at(0).get_uint64());
 ASSERT_EQ(0,DatumValueBuffer::set_bool(values.data(),values.capacity(),0,true));ASSERT_EQ(ObTinyIntType,values.at(0).get_type());ASSERT_TRUE(values.at(0).get_bool());
 values.at(0).meta_.set_type(ObLongTextType);values.at(0).meta_.set_has_lob_header();ASSERT_TRUE(values.at(0).has_lob_header());
 ASSERT_EQ(0,DatumValueBuffer::set_lob_view(values.data(),values.capacity(),0,ObLongTextType,ObString::make_string(large)));ASSERT_FALSE(values.at(0).has_lob_header());
 values.at(0).meta_.set_collation_level(CS_LEVEL_EXPLICIT);ASSERT_EQ(0,values.set_lob_view(0,ObCollectionSQLType,ObString::make_string(large)));ASSERT_EQ(CS_LEVEL_EXPLICIT,values.at(0).get_collation_level());
 DatumValueBuffer deferred;ASSERT_EQ(OB_NOT_INIT,deferred.reserve(1));ASSERT_EQ(OB_INVALID_ARGUMENT,deferred.init(nullptr));ASSERT_EQ(0,deferred.init(&owner));ASSERT_EQ(OB_INIT_TWICE,deferred.init(&owner));ASSERT_EQ(0,deferred.reserve(1));
 FailOwner failure;DatumValueBuffer bad(failure);ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED,bad.reserve(3));ASSERT_EQ(nullptr,bad.data());
}
TEST(NativeRow, recursive_array_clone_and_relocation_do_not_keep_source_pointers) {
 ObArenaAllocator source_owner,owner;auto *outer=DatumParameterArray::alloc(source_owner,2);auto *inner=DatumParameterArray::alloc(source_owner,1);ASSERT_NE(nullptr,outer);ASSERT_NE(nullptr,inner);
 char text[]="source leaf has a long lifetime dependency";ASSERT_EQ(0,inner->data_[0].set_varchar(ObString::make_string(text),source_owner));
 SqlTypeMeta array_meta;array_meta.set_ext();array_meta.set_extend_type(T_EXT_SQL_ARRAY);
 ASSERT_EQ(0,outer->data_[0].alloc_datum_reserved_buff(array_meta,0,source_owner));outer->data_[0].set_type_meta(array_meta);outer->data_[0].set_array_address(inner);
 ASSERT_EQ(0,outer->data_[1].set_int(17,source_owner));DatumParameter value;ASSERT_EQ(0,value.alloc_datum_reserved_buff(array_meta,0,source_owner));value.set_type_meta(array_meta);value.set_array_address(outer);
 DatumValue view=value.value_view();ObNewRow row(&view,1),owned;ASSERT_EQ(0,row.clone(owner,owned));
 const int64_t size=sizeof(ObNewRow)+row.get_deep_copy_size();std::vector<char> flat(size);auto *header=new(flat.data())ObNewRow;int64_t pos=sizeof(ObNewRow);
 ASSERT_EQ(0,header->deep_copy(row,flat.data(),size,pos));ASSERT_EQ(size,pos);std::vector<char> moved=flat;source_owner.reset();std::fill(flat.begin(),flat.end(),char(0xdb));
 ObNewRow *restored=nullptr;pos=0;ASSERT_EQ(0,ObNewRow::construct(moved.data(),moved.size(),pos,restored));ASSERT_EQ(size,pos);
 auto *a=reinterpret_cast<DatumParameterArray *>(restored->cells_[0].get_ext());ASSERT_EQ(2,a->count_);ASSERT_EQ(17,a->data_[1].get_int());
 auto *b=a->data_[0].get_array<DatumParameterArray>();ASSERT_EQ(1,b->count_);ASSERT_EQ(ObString::make_string(text),b->data_[0].get_string());
 auto *own_array=reinterpret_cast<DatumParameterArray *>(owned.cells_[0].get_ext());ASSERT_EQ(17,own_array->data_[1].get_int());
 const int64_t body=sizeof(ObNewRow)+sizeof(DatumValue);pos=body;
 ASSERT_EQ(0,DatumValueBuffer::make_offsets(restored->cells_[0],moved.data(),moved.size(),pos,nullptr,false));ASSERT_EQ(size,pos);
 pos=body;ASSERT_EQ(0,DatumValueBuffer::make_offsets(restored->cells_[0],moved.data(),moved.size(),pos));ASSERT_EQ(size,pos);
 ASSERT_LT(reinterpret_cast<uintptr_t>(restored->cells_[0].datum_.ptr_),moved.size());
 std::vector<char> offsets=moved;std::fill(moved.begin(),moved.end(),char(0xcd));restored=nullptr;pos=0;
 ASSERT_EQ(0,ObNewRow::construct(offsets.data(),offsets.size(),pos,restored));ASSERT_EQ(size,pos);
 a=reinterpret_cast<DatumParameterArray *>(restored->cells_[0].get_ext());b=a->data_[0].get_array<DatumParameterArray>();ASSERT_EQ(ObString::make_string(text),b->data_[0].get_string());ASSERT_EQ(17,a->data_[1].get_int());

}
TEST(NativeRow, owner_clone_failure_and_truncated_construct_do_not_publish) {
 ObArenaAllocator owner;DatumValueBuffer values(owner);ASSERT_EQ(0,values.reserve(1));ASSERT_EQ(0,values.set_int(0,17));ObNewRow row(values.data(),1),out=row;
 FailOwner fail;ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED,row.clone(fail,out));ASSERT_EQ(values.data(),out.cells_);
 const int64_t size=sizeof(ObNewRow)+row.get_deep_copy_size();std::vector<char> flat(size);auto *header=new(flat.data())ObNewRow;int64_t pos=sizeof(ObNewRow);ASSERT_EQ(0,header->deep_copy(row,flat.data(),size,pos));
 auto *old_cells=header->cells_;ObNewRow *result=&row;pos=0;ASSERT_NE(0,ObNewRow::construct(flat.data(),size-1,pos,result));ASSERT_EQ(0,pos);ASSERT_EQ(&row,result);ASSERT_EQ(old_cells,header->cells_);
}
