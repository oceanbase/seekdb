-- SQL-callable batched ANN. The observer must be built with OB_BUILD_CUVS=ON
-- and resolve the configured hipVS/cuVS bridge at runtime.
--
-- Input contract:
--   items(id BIGINT, embedding VECTOR(128))
--   query_vectors(id BIGINT, embedding VECTOR(128))
-- The first input column is the row id; the second is the vector. Both vector
-- columns must have the same dimension.
use vec;

create table if not exists batch_results(
  probe_id bigint,
  neighbor_id bigint,
  distance float,
  rk int
);

call dbms_vector.batch_knn("items", "query_vectors", 10, "batch_results");

select * from batch_results where probe_id = 0 order by rk;
select count(*) from batch_results; -- query count * top-k
