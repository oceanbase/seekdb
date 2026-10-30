#include "common/cell/ob_cell_writer.h"
#include "common/cell/ob_cell_reader.h"
#include "common/row/ob_row_store.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum_parameter_array.h"
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native cell/store tests must not include a complete legacy value"
#endif
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <cstdlib>
using namespace oceanbase::common;
namespace {
std::vector<std::string> fields(const std::string &s){std::istringstream in(s);std::string c;std::vector<std::string> r;while(std::getline(in,c,'\t'))r.push_back(c);return r;}
std::string hex(const char *p,int64_t n){const char*d="0123456789abcdef";std::string s;for(int64_t i=0;i<n;++i){unsigned char c=p[i];s+=d[c>>4];s+=d[c&15];}return s.empty()?"-":s;}
std::string unhex(const std::string &s){if(s=="-")return {};std::string r;for(size_t i=0;i+1<s.size();i+=2)r+=static_cast<char>(std::stoul(s.substr(i,2),nullptr,16));return r;}
std::string wire(const DatumValue &v){char data[4096];int64_t pos=0;int r=DatumParameterCodec::encode_value(v.datum_,v.meta_,data,sizeof(data),pos);return r?"ERROR"+std::to_string(r):hex(data,pos);}
std::string row_wire(const ObNewRow &row){std::string bytes(row.get_serialize_size()+16,'\0');int64_t p=0;int r=row.serialize(bytes.data(),bytes.size(),p);return r?"ERROR"+std::to_string(r):hex(bytes.data(),p);}
std::string fixture(){const char *env=getenv("TEST_SRCDIR");if(env){std::string p=std::string(env)+"/_main/unittest/oblib/row/data/native_cell.golden";std::ifstream f(p);if(f)return p;}return "unittest/oblib/row/data/native_cell.golden";}
struct ReservedTag {using type=ObFixedArray<int64_t,ObIAllocator> ObRowStore::*;friend type member(ReservedTag);};
template<typename Tag,typename Tag::type Value>struct PrivateAccess{friend typename Tag::type member(Tag){return Value;}};
template struct PrivateAccess<ReservedTag,&ObRowStore::reserved_columns_>;
class ToggleOwner final:public ObIAllocator {
public:
 bool fail_=false;std::unordered_set<void *> live_;
 void *alloc(int64_t n)override{if(fail_)return nullptr;void*p=std::malloc(n);if(p)live_.insert(p);return p;}
 void *alloc(int64_t n,const ObMemAttr&)override{return alloc(n);}
 void free(void*p)override{if(p){EXPECT_EQ(1,live_.erase(p));std::free(p);}}
 ~ToggleOwner(){EXPECT_TRUE(live_.empty());for(void*p:live_)std::free(p);}
};
}
TEST(NativeCellStore, actual_old269_dense_sparse_bytes_clones_and_read_values){
 std::ifstream input(fixture());ASSERT_TRUE(input.good());std::string line;int cases=0;
 while(std::getline(input,line)){
  auto f=fields(line);ASSERT_EQ(14,f.size());SCOPED_TRACE(f[1]+"/"+f[2]);++cases;ObArenaAllocator owner;DatumValue value,clone,parsed;
  std::string encoded=unhex(f[4]);int64_t pos=0;ASSERT_EQ(0,DatumParameterCodec::decode_value(encoded.data(),encoded.size(),pos,owner,value.datum_,value.meta_));ASSERT_EQ(encoded.size(),pos);
  const int64_t capacity=std::stoll(f[3]);std::vector<char> bytes(capacity);ObCellWriter writer;char scalar[16];ASSERT_EQ(0,clone.set_int(999,owner));
  int ret=writer.init(bytes.data(),capacity,static_cast<ObCompactStoreType>(std::stoi(f[2])));if(ret==0)ret=writer.append(42,value,&clone,scalar);
  ASSERT_EQ(std::stoi(f[5]),ret);ASSERT_EQ(f[6],wire(clone));const int append=ret;int finish=ret;if(ret==0)finish=writer.row_finish();ASSERT_EQ(std::stoi(f[7]),finish);ASSERT_EQ(f[8],hex(bytes.data(),writer.size()));
  int readret=append;uint64_t col=0;bool end=false;int64_t consumed=0;ASSERT_EQ(0,parsed.set_int(77,owner));std::string parsed_wire=wire(parsed);
  if(append==0&&finish==0){ObCellReader reader;readret=reader.init(bytes.data(),writer.size(),static_cast<ObCompactStoreType>(std::stoi(f[2])));if(readret==0)readret=reader.next_cell();if(readret==0)readret=reader.get_cell(col,parsed,&end);consumed=reader.get_pos();parsed_wire=wire(parsed);}
  ASSERT_EQ(std::stoi(f[9]),readret);ASSERT_EQ(std::stoll(f[10]),consumed);ASSERT_EQ(std::stoull(f[11]),col);ASSERT_EQ(std::stoi(f[12])!=0,end);ASSERT_EQ(f[13],parsed_wire);
 }
 ASSERT_EQ(269,cases);
}
TEST(NativeCellStore, rowstore_modes_reserved_values_and_serialization_own_native_payloads){
 for(bool compact:{true,false}){
  SCOPED_TRACE(compact);ObArenaAllocator source,owner,destination;DatumValueBuffer values(source);ASSERT_EQ(0,values.reserve(3));ASSERT_EQ(0,values.set_int(0,-17));char text[]="long string copied into the row store";ASSERT_EQ(0,values.set_varchar_view(1,ObString::make_string(text)));values.at(1).set_collation_type(CS_TYPE_UTF8MB4_BIN);ASSERT_EQ(0,values.set_null(2));ObNewRow row(values.data(),3);
  ObRowStore store(owner,"NativeStore",compact);ASSERT_EQ(0,store.init_reserved_column_count(2));auto &columns=store.*member(ReservedTag{});ASSERT_EQ(0,columns.push_back(0));ASSERT_EQ(0,columns.push_back(1));
  const ObRowStore::StoredRow *stored=nullptr;ASSERT_EQ(0,store.add_row(row,stored,123,true));ASSERT_EQ(2,stored->reserved_cells_count_);ASSERT_EQ(-17,stored->reserved_cells_[0].get_int());ASSERT_EQ(ObString::make_string(text),stored->reserved_cells_[1].get_string());const auto expected=row_wire(row);std::fill(text,text+sizeof(text)-1,'x');ASSERT_NE(ObString::make_string(text),stored->reserved_cells_[1].get_string());
  const int64_t size=store.get_serialize_size();std::vector<char> bytes(size),again(size);int64_t pos=0,twice=0;ASSERT_EQ(0,store.serialize(bytes.data(),bytes.size(),pos));ASSERT_EQ(size,pos);ASSERT_EQ(0,store.serialize(again.data(),again.size(),twice));ASSERT_EQ(bytes,again);ASSERT_EQ(-17,stored->reserved_cells_[0].get_int());
  ObRowStore restored(destination,"NativeRestored",compact);pos=0;ASSERT_EQ(0,restored.deserialize(bytes.data(),bytes.size(),pos));ASSERT_EQ(size,pos);store.reset();std::fill(bytes.begin(),bytes.end(),char(0xdb));
  auto iterator=restored.begin();ObRowStore::StoredRow *r=nullptr;DatumValueBuffer output(destination);ASSERT_EQ(0,output.reserve(3));ObNewRow out(output.data(),3);ObNewRow *view=nullptr;
  if(compact){ASSERT_EQ(0,iterator.get_next_row(out,nullptr,&r));view=&out;}else{ASSERT_EQ(0,iterator.get_next_row(view,&r));}
  ASSERT_EQ(expected,row_wire(*view));ASSERT_EQ(123,r->payload_);ASSERT_EQ(-17,r->reserved_cells_[0].get_int());ASSERT_EQ(view->get_cell(1).get_string(),r->reserved_cells_[1].get_string());
 }
}
TEST(NativeCellStore, compact_projector_flatten_and_independent_iterator_buffers){
 ObArenaAllocator owner;DatumValueBuffer values(owner);ASSERT_EQ(0,values.reserve(2));ASSERT_EQ(0,values.set_int(0,17));ASSERT_EQ(0,values.set_int(1,29));int32_t projector[]={1,0,1};ObNewRow row(values.data(),2);row.projector_=projector;row.projector_size_=3;ObRowStore store(owner);ASSERT_EQ(0,store.add_row(row));ASSERT_EQ(0,values.set_int(0,71));ASSERT_EQ(0,values.set_int(1,92));ASSERT_EQ(0,store.add_row(row));
 auto a=store.begin();auto b=a;DatumValue left_values[3],right_values[3];ObNewRow left(left_values,3),right(right_values,3);ASSERT_EQ(0,a.get_next_row(left));ASSERT_EQ(29,left.get_cell(0).get_int());ASSERT_EQ(0,b.get_next_row(right));ASSERT_EQ(29,right.get_cell(0).get_int());ASSERT_EQ(0,b.get_next_row(right));ASSERT_EQ(92,right.get_cell(0).get_int());ASSERT_EQ(29,left.get_cell(0).get_int());
}
TEST(NativeCellStore, iterator_reserve_oom_does_not_advance_row_and_reuse_has_no_per_row_alloc){
 ToggleOwner owner;ObArenaAllocator source;DatumValueBuffer values(source);ASSERT_EQ(0,values.reserve(1));ASSERT_EQ(0,values.set_int(0,17));ObNewRow row(values.data(),1);{
  ObRowStore store(owner);ASSERT_EQ(0,store.add_row(row));ASSERT_EQ(0,store.add_row(row));auto iterator=store.begin();DatumValue output;ObNewRow out(&output,1);owner.fail_=true;ASSERT_EQ(OB_ALLOCATE_MEMORY_FAILED,iterator.get_next_row(out));owner.fail_=false;ASSERT_EQ(0,iterator.get_next_row(out));ASSERT_EQ(17,out.get_cell(0).get_int());const size_t allocated=owner.live_.size();ASSERT_EQ(0,iterator.get_next_row(out));ASSERT_EQ(allocated,owner.live_.size());
 }
 ASSERT_TRUE(owner.live_.empty());
}
TEST(NativeCellStore, raw_rowstore_nested_array_serialization_relocates_without_source_owner){
 ObArenaAllocator source,owner,destination;auto *array=DatumParameterArray::alloc(source,2);ASSERT_NE(nullptr,array);ASSERT_EQ(0,array->data_[0].set_int(17,source));const ObString text=ObString::make_string("array string has independently owned bytes");ASSERT_EQ(0,array->data_[1].set_varchar(text,source));SqlTypeMeta meta;meta.set_ext();meta.set_extend_type(T_EXT_SQL_ARRAY);DatumParameter parameter;parameter.set_type_meta(meta);ASSERT_EQ(0,parameter.alloc_datum_reserved_buff(meta,-1,source));parameter.set_array_address(array);DatumValue value=parameter.value_view();ObNewRow row(&value,1);
 ObRowStore store(owner,"ArrayStore",false);ASSERT_EQ(0,store.add_row(row));source.reset();std::vector<char> bytes(store.get_serialize_size());int64_t pos=0;ASSERT_EQ(0,store.serialize(bytes.data(),bytes.size(),pos));ObRowStore restored(destination,"ArrayRestore",false);pos=0;ASSERT_EQ(0,restored.deserialize(bytes.data(),bytes.size(),pos));store.reset();std::fill(bytes.begin(),bytes.end(),char(0xee));ObNewRow *out=nullptr;auto iterator=restored.begin();ASSERT_EQ(0,iterator.get_next_row(out,nullptr));auto *copy=reinterpret_cast<DatumParameterArray *>(out->get_cell(0).get_ext());ASSERT_EQ(17,copy->data_[0].get_int());ASSERT_EQ(text,copy->data_[1].get_string());
}

TEST(NativeCellStore, declared_int_number_and_varchar_nulls_use_ordinary_null_compact_bytes){
 for(auto mode:{DENSE,SPARSE}){
  char ordinary_bytes[64];ObCellWriter ordinary;DatumValue plain_null;
  ASSERT_EQ(0,ordinary.init(ordinary_bytes,sizeof(ordinary_bytes),mode));
  ASSERT_EQ(0,ordinary.append(42,plain_null));ASSERT_EQ(0,ordinary.row_finish());
  const std::string expected=hex(ordinary_bytes,ordinary.size());
  for(auto type:{ObIntType,ObNumberType,ObVarcharType}){
   SCOPED_TRACE(int(type));
   SCOPED_TRACE(int(mode));DatumValue typed;
   if(type==ObIntType)typed.meta_.set_int();else if(type==ObNumberType)typed.meta_.set_number();else typed.meta_.set_varchar();
   typed.datum_.set_null();ASSERT_EQ(nullptr,typed.datum_.ptr_);
   char bytes[64],slot[16];DatumValue clone;ObCellWriter writer;
   ASSERT_EQ(0,writer.init(bytes,sizeof(bytes),mode));ASSERT_EQ(0,writer.append(42,typed,&clone,slot));
   EXPECT_TRUE(clone.is_null());EXPECT_EQ(type,clone.get_type());
   ASSERT_EQ(0,writer.row_finish());ASSERT_EQ(expected,hex(bytes,writer.size()));
   ObCellReader reader;ASSERT_EQ(0,reader.init(bytes,writer.size(),mode));ASSERT_EQ(0,reader.next_cell());
   uint64_t column=0;DatumValue parsed;bool end=false;ASSERT_EQ(0,reader.get_cell(column,parsed,&end));
   EXPECT_FALSE(end);EXPECT_TRUE(parsed.is_null());EXPECT_EQ(ObNullType,parsed.get_type());
   EXPECT_EQ(mode==SPARSE?uint64_t(42):OB_INVALID_ID,column);
  }
 }
}
TEST(NativeCellStore, compact_rowstore_typed_nulls_have_safe_reserved_values_and_reader){
 ObArenaAllocator owner;DatumValue typed[3],plain[3];
 typed[0].meta_.set_int();typed[1].meta_.set_number();typed[2].meta_.set_varchar();
 for(auto&v:typed)v.datum_.set_null();ObNewRow source(typed,3),ordinary(plain,3);
 ObRowStore store(owner),reference(owner);ASSERT_EQ(0,store.init_reserved_column_count(3));
 auto&columns=store.*member(ReservedTag{});for(int64_t i=0;i<3;++i)ASSERT_EQ(0,columns.push_back(i));
 const ObRowStore::StoredRow*stored=nullptr,*expected=nullptr;
 ASSERT_EQ(0,reference.add_row(ordinary,expected,0,true));ASSERT_EQ(0,store.add_row(source,stored,0,true));
 ASSERT_EQ(expected->get_compact_row(),stored->get_compact_row());ASSERT_EQ(3,stored->reserved_cells_count_);
 for(int64_t i=0;i<3;++i){EXPECT_TRUE(stored->reserved_cells_[i].is_null());EXPECT_EQ(typed[i].get_type(),stored->reserved_cells_[i].get_type());}
 DatumValue output[3];ObNewRow decoded(output,3);auto iterator=store.begin();ASSERT_EQ(0,iterator.get_next_row(decoded));
 for(int64_t i=0;i<3;++i){EXPECT_TRUE(decoded.get_cell(i).is_null());EXPECT_EQ(ObNullType,decoded.get_cell(i).get_type());}
}
