SELECT @@session.ob_hnsw_ef_search;
CREATE VECTOR INDEX `ix_gpu_r00` ON `amd107_20261028100345_30525`.`gpu_r00`(c2) WITH (distance=l2,type=hnsw,lib=cuvs,m=64,ef_construction=400,ef_search=1000);
