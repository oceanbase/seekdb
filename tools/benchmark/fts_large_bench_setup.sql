-- 大规模全文索引压测：库表初始化
-- 由 fts_large_bench.sh 调用，一般无需单独执行

DROP DATABASE IF EXISTS fts_large_bench;
CREATE DATABASE fts_large_bench;
USE fts_large_bench;

SET NAMES utf8mb4;
SET ob_query_timeout = 100000000000;
SET ob_trx_timeout = 100000000000;

CREATE TABLE docs (
  id         BIGINT AUTO_INCREMENT PRIMARY KEY,
  category   INT NOT NULL,
  title      VARCHAR(256) NOT NULL,
  content    TEXT NOT NULL,
  title_en   VARCHAR(256) NOT NULL,
  content_en TEXT NOT NULL
) DEFAULT CHARSET = utf8mb4 COMMENT='FTS large benchmark table';
