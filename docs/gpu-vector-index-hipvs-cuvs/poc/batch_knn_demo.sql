-- dbms_vector.batch_knn PoC demo (Option B: SQL-callable batched ANN).
-- Convention: index/probe tables have 2 cols (col0=id int, col1=vector);
-- out_table pre-created as (probe_id bigint, neighbor_id bigint, distance float, rk int).
-- Observer must be built with OB_BUILD_CUVS=ON and find the bridge/hipVS libraries at runtime.
use vec;
-- tables: t10k(id,v vector(128)) 10000 rows; probes_q(id,v) 100 rows (see bk_setup.sql)
create table if not exists bk_out(probe_id bigint, neighbor_id bigint, distance float, rk int);
-- one GPU batch call for all probes -> writes neighbors to bk_out
call dbms_vector.batch_knn("t10k", "probes_q", 10, "bk_out");
-- inspect
select * from bk_out where probe_id = 0 order by rk;
select count(*) from bk_out;               -- = probes * topk
