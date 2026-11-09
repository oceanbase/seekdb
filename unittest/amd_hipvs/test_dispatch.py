"""Exercise actual extracted C++ dispatch functions, with observable backend doubles."""
from pathlib import Path
import hashlib,json,subprocess,time,tempfile,os,shutil

ROOT=Path(__file__).resolve().parents[2]
TEMP=tempfile.TemporaryDirectory(prefix="seekdb-amd-dispatch-")
OUT=Path(TEMP.name)
def function(source,needle):
    start=source.rfind('\n',0,source.index(needle))+1
    opening=source.index('{',start);depth=1;pos=opening+1
    while depth:
        if source[pos]=='{':depth+=1
        elif source[pos]=='}':depth-=1
        pos+=1
    return source[start:pos]+'\n'

STUBS=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
using VectorIndexPtr=void*;
constexpr int OB_SUCCESS=0,OB_INVALID_ARGUMENT=-1;
#define OB_FAIL(x) ((ret=(x))!=0)
#define LOG_WARN(...) ((void)0)
enum ObVectorIndexAlgorithmLib { VIAL_VSAG=0,VIAL_OB=1,VIAL_MAX=2 };
enum ObVectorIndexDistAlgorithm { VIDA_L2=0,VIDA_IP=1 };
enum ObVectorIndexAlgorithmType { VIAT_HNSW=0,VIAT_HGRAPH=1 };
struct ObVectorIndexParam { ObVectorIndexAlgorithmLib lib_; ObVectorIndexDistAlgorithm dist_algorithm_; ObVectorIndexAlgorithmType type_; int extra_info_actual_size_; };
int marks=0,gpu_calls=0,fallback_calls=0,vsag_calls=0;
namespace oceanbase { namespace common { namespace obvsag {
void mark_cuvs_index(void*) {++marks;}
}}}
enum IndexType { HNSW_TYPE=0,HGRAPH_TYPE=1 };
struct FilterInterface {};
struct Dataset { static std::shared_ptr<Dataset> Make(){return std::make_shared<Dataset>();} Dataset* NumElements(int){return this;} Dataset* Dim(int){return this;} Dataset* Float32Vectors(float*){return this;} Dataset* Owner(bool){return this;} };
using DatasetPtr=std::shared_ptr<Dataset>;
namespace vsag { using Dataset=::Dataset; }
bool ob_cuvs_marked(void*) {return true;}
void ob_vsag_trace(const char*,const void*,long,long) {}
int construct_vsag_search_param(uint8_t,int,bool,char *out) {out[0]='\0';return 0;}
bool ob_cuvs_try_search(void*,void*,float*,int,int64_t,const float*& d,const int64_t*& ids,int64_t& n,void*,bool) {
  static int64_t stale[1]={999};static float dist[1]={0};++gpu_calls;ids=stale;d=dist;n=1;return true;
}
struct HnswIndexHandler {
  const char* get_metric(){return "l2";} void* get_allocator(){return nullptr;}
  int get_index_type(){return HNSW_TYPE;} uint64_t get_extra_info_size(){return 0;}
  int64_t get_index_number(){return 100;} int ensure_vsag_fallback(){++fallback_calls;return 0;}
  int knn_search(DatasetPtr,int64_t,const std::string&,const float*& dist,const int64_t*& ids,int64_t& n,float,IndexType,FilterInterface*,bool,bool,const char*& extras,void*& ctx,bool last,void*) {
    static int64_t batch[1];static float distances[1]={0};static int state=0;
    ++vsag_calls;if(!ctx){state=0;ctx=&state;}batch[0]=++(*static_cast<int*>(ctx));
    ids=batch;dist=distances;n=1;extras="kept";if(last)ctx=nullptr;return 0;
  }
  int knn_search(DatasetPtr,int64_t,const std::string&,const float*& dist,const int64_t*& ids,int64_t& n,float,IndexType,FilterInterface*,bool,bool,const char*& extras,void*,float) {
    static int64_t batch[1]={42};static float distances[1]={0};++vsag_calls;ids=batch;dist=distances;n=1;extras="kept";return 0;
  }
};
'''

MAIN=r'''
int main(int argc,char** argv){
  const std::string which=argc>1?argv[1]:"";
  if(which=="extra-info-gate"){
    int handle=1;ObVectorIndexParam p{static_cast<ObVectorIndexAlgorithmLib>(3),VIDA_L2,VIAT_HGRAPH,8};
    ob_mark_cuvs_if_needed(&p,&handle);if(marks)return 11;
    p.extra_info_actual_size_=0;ob_mark_cuvs_if_needed(&p,&handle);if(marks)return 12;
    p.extra_info_actual_size_=8;p.type_=VIAT_HNSW;ob_mark_cuvs_if_needed(&p,&handle);if(marks)return 12;
    p.extra_info_actual_size_=0;ob_mark_cuvs_if_needed(&p,&handle);if(marks!=1)return 13;
    HnswIndexHandler h;void* idx=&h;float q[3]={0};const float* d=nullptr;const int64_t* ids=nullptr;int64_t n=0;const char* extras=nullptr;
    for(int i=0;i<2;++i){
      if(knn_search(idx,q,3,1,d,ids,n,100,i==0,extras,nullptr,false,i==1,nullptr,1.f,0.f))return 14;
      if(gpu_calls||vsag_calls!=i+1||fallback_calls!=i+1||!extras||std::strcmp(extras,"kept"))return 15;
    }
  }else if(which=="iterative-continuation"){
    HnswIndexHandler h;void* idx=&h;float q[3]={0};const float* d=nullptr;const int64_t* ids=nullptr;int64_t n=0;const char* extras=nullptr;void* ctx=nullptr;
    if(knn_search(idx,q,3,1,d,ids,n,100,false,extras,nullptr,false,false,1.f,ctx,false,nullptr)||n!=1||ids[0]!=1||!ctx)return 21;
    if(knn_search(idx,q,3,1,d,ids,n,100,false,extras,nullptr,false,false,1.f,ctx,true,nullptr)||n!=1||ids[0]!=2||ctx)return 22;
    if(gpu_calls||vsag_calls!=2||fallback_calls!=2)return 23;
  }else return 30;
  std::cout<<which<<" passed\n";
}
'''

def sources(root):
    p=(root/'src/observer/vector_index/ob_plugin_vector_index_adaptor.cpp').read_text()
    a=(root/'src/oblib/lib/vector/ob_vsag_adaptor.cpp').read_text()
    helper=function(p,'static inline void ob_mark_cuvs_if_needed')
    # Both production overloads are compiled verbatim, not a reimplementation.
    simple=function(a,'int knn_search(VectorIndexPtr &index_handler, float *query_vector,')
    at=a.index('bool use_extra_info_filter, float valid_ratio, void *&iter_ctx')
    iterative=function(a[a.rfind('int knn_search(',0,at):],'int knn_search(')
    return STUBS+helper+simple+iterative+MAIN

compiler=os.environ.get("CXX") or shutil.which("clang++") or shutil.which("c++")
assert compiler,"C++20 compiler required"
receipts=[]
for label,root in [('fixed',ROOT)]:
    text=sources(root);source=OUT/(label+'.cpp');source.write_text(text);exe=OUT/label
    result=subprocess.run([compiler,'-std=c++20','-O0',str(source),'-o',str(exe)],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    for case in ['extra-info-gate','iterative-continuation']:
        r=subprocess.run([str(exe),case],capture_output=True,text=True)
        receipts.append({'version':label,'case':case,'exit':r.returncode,'stdout':r.stdout,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest()})
        assert (r.returncode==0)==(label=='fixed'),receipts[-1]
report={'pass':True,'production_functions_extracted_verbatim':True,'gpu_execution':False,'tests':receipts,'finished':time.time()}
(OUT/'receipt.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
