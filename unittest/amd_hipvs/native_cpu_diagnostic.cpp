/* Copyright (c) 2025 OceanBase. Licensed under the Apache License, Version 2.0. */
// CPU-only diagnosis of ANN misses versus stored vector/label completeness.
// This program never measures GPU or SQL end-to-end performance.
#include <vsag/vsag.h>
#include <vsag/constants.h>
#include <vsag/filter.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

template<class T> void ok(const T& x) { if (!x.has_value()) throw std::runtime_error(x.error().message); }
void require(bool b, const char* msg) { if (!b) throw std::runtime_error(msg); }
struct AcceptAll: vsag::Filter { bool CheckValid(int64_t) const override { return true; } };
template<class T> std::vector<T> read(const std::string& path, size_t n) {
  std::vector<T> data(n); std::ifstream f(path,std::ios::binary);
  f.read(reinterpret_cast<char*>(data.data()),n*sizeof(T));
  require(bool(f)&&f.peek()==std::char_traits<char>::eof(),"input length mismatch"); return data;
}
int main(int argc,char**argv) try {
  require(argc>=3 && argc<=5,"usage: probe directory input|pk [M] [ef_construction]");
  const std::string root=argv[1], order=argv[2]; const int n=10000,d=128;
  const int effective_m=argc>=4 ? std::stoi(argv[3]) : 16;
  const int efc=argc==5 ? std::stoi(argv[4]) : 200;
  require((order=="input" || order=="pk") && effective_m>=5 && effective_m<=128 &&
          efc>=effective_m && efc<=1000,"invalid diagnostic parameters");
  auto v=read<float>(root+"/"+order+".f32",n*d);
  auto ids=read<int64_t>(root+"/"+order+".i64",n);
  auto qs=read<float>(root+"/queries.f32",100*d);
  const std::string params=std::string(R"({"dim":128,"dtype":"float32","metric_type":"l2","use_old_serial_format":true,"hnsw":{"ef_construction":)")+std::to_string(efc)+",\"ef_search\":200,\"max_degree\":"+std::to_string(effective_m)+"}}";
  auto created=vsag::Factory::CreateIndex("hnsw",params);ok(created);auto idx=created.value();
  for(int begin=0;begin<n;begin+=500) {
    auto data=vsag::Dataset::Make()->Owner(false)->Dim(d)->NumElements(500)->Ids(ids.data()+begin)->Float32Vectors(v.data()+begin*d);
    auto result=idx->Add(data);ok(result);require(result.value().empty(),"Add skipped ID");
  }
  std::cout<<"VERSION="<<vsag::version()<<" order="<<order<<" M="<<effective_m<<" ef_construction="<<efc<<" count="<<idx->GetNumElements()<<std::endl;
  const vsag::FilterPtr all=std::make_shared<AcceptAll>();
  const auto query=[&](const float* p,int ef,int k) {
    auto data=vsag::Dataset::Make()->Owner(false)->Dim(d)->NumElements(1)->Float32Vectors(p);
    auto out=idx->KnnSearch(data,k,"{\"hnsw\":{\"ef_search\":"+std::to_string(ef)+",\"skip_ratio\":0.7}}",all);
    ok(out);require(out.value()->GetDim()==k,"query count mismatch");return out.value();
  };
  std::vector<int> misses(n);std::iota(misses.begin(),misses.end(),0);
  for(int ef:{200,1000,10000}) {
    std::vector<int> next;
    for(int i:misses) {
      auto x=query(v.data()+i*d,ef,ef>1000 ? ef/10 : 1);
      if(x->GetIds()[0]!=ids[i] || std::abs(x->GetDistances()[0])>1e-6) next.push_back(i);
    }
    std::cout<<"SELF ef="<<ef<<" remaining="<<next.size()<<std::endl;
    misses=std::move(next);
  }
  for(int i:misses)std::cout<<"MISSED row="<<i<<" id="<<ids[i]<<std::endl;
  int hits=0;
  for(int q=0;q<100;q++) {
    std::vector<std::pair<double,int64_t>> truth;
    for(int i=0;i<n;i++){double dist=0;for(int j=0;j<d;j++){double t=double(qs[q*d+j])-v[i*d+j];dist+=t*t;}truth.emplace_back(dist,ids[i]);}
    std::partial_sort(truth.begin(),truth.begin()+10,truth.end());
    auto x=query(qs.data()+q*d,200,10);
    for(int k=0;k<10;k++)for(int t=0;t<10;t++)hits+=(x->GetIds()[k]==truth[t].second);
  }
  std::cout<<"RECALL="<<hits/1000.0<<std::endl;
  try{int stored=0;for(auto id:ids)stored+=idx->CheckIdExist(id);std::cout<<"CheckIdExist="<<stored<<std::endl;}
  catch(const std::exception&e){std::cout<<"CheckIdExist unsupported: "<<e.what()<<std::endl;}
  try{auto raw=idx->GetRawVectorByIds(ids.data(),n);ok(raw);std::cout<<"RAW_VECTOR_EQUAL="<<(0==std::memcmp(raw.value()->GetFloat32Vectors(),v.data(),v.size()*sizeof(float)))<<std::endl;}
  catch(const std::exception&e){std::cout<<"GetRaw unsupported: "<<e.what()<<std::endl;}
  auto exported=idx->Serialize();ok(exported);
  auto bin=exported.value().Get(vsag::HNSW_DATA); require(bin.size>96,"missing native HNSW bytes");
  std::ofstream saved(root+"/"+order+"-m"+std::to_string(effective_m)+"-efc"+std::to_string(efc)+".hnsw",std::ios::binary);saved.write(reinterpret_cast<char*>(bin.data.get()),bin.size);
  require(bool(saved),"native export write failed");
  std::ofstream stream(root+"/"+order+"-m"+std::to_string(effective_m)+"-efc"+std::to_string(efc)+".stream",std::ios::binary);
  auto stream_result=idx->Serialize(stream);ok(stream_result);
  require(bool(stream),"stream export write failed");
  std::cout<<"SERIALIZED_BYTES="<<bin.size<<std::endl;
  return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}
