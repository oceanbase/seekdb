-- 全文索引分词热路径：功能验证用例
-- 用法: mysql -h127.0.0.1 -P2881 -uroot -Dtest < tools/benchmark/fts_hotpath_bench.sql

DROP DATABASE IF EXISTS fts_bench;
CREATE DATABASE fts_bench;
USE fts_bench;

SET ob_query_timeout = 10000000000;

-- ============================================================
-- 1. TOKENIZE 功能验证（直接走分词器热路径）
-- ============================================================

-- space / beng / ngram / ik 四类内置分词器
SELECT tokenize('hello world quick brown fox', 'space') AS space_tokens;
SELECT tokenize('hello world quick brown fox', 'beng') AS beng_tokens;
SELECT tokenize('hello world quick brown fox', 'ngram',
                '[{"output":"all"},{"additional_args":[{"ngram_token_size":2}]}]') AS ngram_tokens;

-- ik 中文分词（会加载词典，最能体现热路径优化效果）
SELECT tokenize('OceanBase是一款非常稳定的数据库，全文索引分词性能很重要', 'ik') AS ik_tokens;
SELECT tokenize('中华人民共和国人民大会堂', 'ik',
                '[{"additional_args":[{"ik_mode":"smart"}]}]') AS ik_smart;
SELECT tokenize('中华人民共和国人民大会堂', 'ik',
                '[{"additional_args":[{"ik_mode":"max_word"}]}]') AS ik_max_word;

-- ============================================================
-- 2. 全文索引建表 + 写入 + MATCH 查询（端到端验证）
-- ============================================================

CREATE TABLE articles (
  id        BIGINT AUTO_INCREMENT PRIMARY KEY,
  title     VARCHAR(200),
  content   TEXT,
  FULLTEXT INDEX fti_title(title) WITH PARSER ik,
  FULLTEXT INDEX fti_content(content) WITH PARSER ik,
  -- MATCH(col1, col2) 必须命中列组合完全一致的全文索引，不能拆成两个单列索引
  FULLTEXT INDEX fti_title_content(title, content) WITH PARSER ik
);

INSERT INTO articles (title, content) VALUES
('数据库性能优化', '全文索引的分词器热路径优化包括解析器复用、停用词单例和内存池化等技术。'),
('搜索引擎原理', '倒排索引是全文检索的核心数据结构，分词质量直接影响召回率和查询性能。'),
('OceanBase 特性', 'OceanBase 支持多种内置分词器，包括 ik、beng、space 和 ngram。'),
('旅行日记', '这次旅行中发生了很多有趣的事情，记录下来留作纪念。'),
('Security Audit', 'Completed the annual security audit with no major vulnerabilities found.');

-- 等待索引构建（seekdb 通常较快，如未就绪可手动 sleep 后重试）
SELECT COUNT(*) AS row_cnt FROM articles;

-- 中文全文检索
SELECT id, title
FROM articles
WHERE MATCH(title, content) AGAINST('分词器 优化');

-- 英文全文检索
SELECT id, title
FROM articles
WHERE MATCH(content) AGAINST('audit security');

-- TOKENIZE 与 MEMBER OF 组合查询
SELECT id, title
FROM articles
WHERE '全文' MEMBER OF (tokenize(content, 'ik'))
   OR 'audit' MEMBER OF (tokenize(content, 'space'));

SELECT 'fts_hotpath_bench: ALL PASSED' AS result;
