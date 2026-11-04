SELECT @@session.ob_hnsw_ef_search;
CREATE VECTOR INDEX `ix_cpu_r01` ON `amd107_20261028094650_23351`.`cpu_r01`(c2) WITH (distance=l2,type=hnsw,lib=vsag,m=64,ef_construction=400,ef_search=1000);
