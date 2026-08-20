-- [hipVS/cuVS] GPU runtime smoke test for declarative lib=cuvs (requires GPU + OB_BUILD_CUVS=ON).
-- Not a CI test (needs an AMD gfx1100 GPU + the bridge .so). Run against a live observer
-- started without any backend-selecting environment variable, with
-- OB_VSAG_TRACE=1 OB_VSAG_TRACE_FILE=/path/trace.log to observe the code path.
--
-- Expected: queries on the lib=cuvs table produce cuvs_serve (GPU) in the trace;
--           queries on the lib=vsag table produce only knn_simple (CPU/VSAG).
use test;
alter system set ob_vector_memory_limit_percentage=30;
drop table if exists t_cuvs; drop table if exists t_vsag;
create table t_cuvs(c1 int primary key, c2 vector(128),
  vector index idx_c(c2) with (distance=l2, type=hnsw, lib=cuvs, m=16, ef_construction=200, ef_search=64));
create table t_vsag(c1 int primary key, c2 vector(128),
  vector index idx_v(c2) with (distance=l2, type=hnsw, lib=vsag, m=16, ef_construction=200, ef_search=64));
-- insert >= 256 rows into each (see bench/smoke_libcuvs.sql generator), then:
--   select c1 from t_cuvs order by l2_distance(c2, <literal-vector>) approximate limit 10;  -- GPU
--   select c1 from t_vsag order by l2_distance(c2, <literal-vector>) approximate limit 10;  -- CPU
-- Verify: grep -c cuvs_serve trace.log  (>0 only after the t_cuvs query).
