/*
 * 混合搜索实现指南和 SQL 示例
 * ================================
 * 
 * 本文档提供了如何使用混合搜索功能的详细说明和 SQL 示例。
 */

-- ========================================================
-- 第一部分：表结构设计
-- ========================================================

-- 创建包含向量和全文索引的表
CREATE TABLE documents (
    id INT PRIMARY KEY,
    title VARCHAR(255),
    content TEXT,
    embedding VECTOR(384),  -- 384 维向量
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    -- 全文索引配置
    FULLTEXT INDEX idx_content(content) WITH PARSER jieba,
    
    -- 向量索引配置
    -- DISTANCE=l2: 使用 L2 欧几里得距离
    -- TYPE=hnsw: 使用 HNSW (Hierarchical Navigable Small Worlds) 算法
    -- LIB=vsag: 使用 VSAG 向量搜索库
    VECTOR INDEX idx_embedding(embedding) WITH(DISTANCE=l2, TYPE=hnsw, LIB=vsag)
) ORGANIZATION = HEAP;

-- ========================================================
-- 第二部分：数据插入示例
-- ========================================================

-- 插入示例数据
INSERT INTO documents (id, title, content, embedding) VALUES
(1, 'Artificial Intelligence Overview', 
 'Machine learning is a branch of artificial intelligence that enables systems to learn and improve from experience without being explicitly programmed. It focuses on developing computer programs that can access data and use it to learn for themselves.',
 VECTOR('[0.1, 0.2, 0.3, ..., 0.384]')),

(2, 'Deep Learning Fundamentals',
 'Deep learning is a subset of machine learning that uses artificial neural networks with multiple layers. It has revolutionized computer vision, natural language processing, and many other AI applications.',
 VECTOR('[0.15, 0.25, 0.35, ..., 0.385]')),

(3, 'Vector Database Technology',
 'Vector databases are specialized databases designed for efficient storage, retrieval, and similarity search of vector embeddings. They support various distance metrics including L2, cosine similarity, and inner product.',
 VECTOR('[0.2, 0.3, 0.4, ..., 0.386]')),

(4, 'Natural Language Processing',
 'Natural language processing (NLP) is a subfield of linguistics, computer science, and artificial intelligence concerned with the interactions between computers and human language. It is used to apply machine learning algorithms to text and speech.',
 VECTOR('[0.12, 0.22, 0.32, ..., 0.387]')),

(5, 'Computer Vision Applications',
 'Computer vision is an interdisciplinary scientific field that deals with how digital images and videos can be used to extract high-level understanding from digital images and videos. It seeks to automate tasks that the human visual system can do.',
 VECTOR('[0.18, 0.28, 0.38, ..., 0.388]'));

-- ========================================================
-- 第三部分：RRF 融合方法的 SQL 示例
-- ========================================================

-- 方案 1.1：基础 RRF 融合查询
-- 用途：需要自动规范化，对异常值鲁棒
-- 参数说明：
--   rank_constant: 60（较大的值对低排名文档更友好）
--   rank_window_size: 100（从 100 个结果中融合）

EXPLAIN SELECT 
    doc_id,
    fts_score,
    vector_score,
    fts_rank,
    vector_rank,
    final_score
FROM (
    WITH fts_results AS (
        SELECT 
            id AS doc_id,
            MATCH(content) AGAINST('artificial intelligence machine learning' IN NATURAL LANGUAGE MODE) AS fts_score,
            ROW_NUMBER() OVER (ORDER BY MATCH(content) AGAINST('artificial intelligence machine learning' IN NATURAL LANGUAGE MODE) DESC) AS fts_rank
        FROM documents
        WHERE MATCH(content) AGAINST('artificial intelligence machine learning' IN NATURAL LANGUAGE MODE)
        LIMIT 100
    ),
    vector_results AS (
        SELECT 
            id AS doc_id,
            1.0 / (1.0 + l2_distance(embedding, '[0.15, 0.25, ...]')) AS vector_score,
            ROW_NUMBER() OVER (ORDER BY l2_distance(embedding, '[0.15, 0.25, ...]') ASC) AS vector_rank
        FROM documents
        LIMIT 100
    ),
    rrf_scores AS (
        SELECT 
            COALESCE(f.doc_id, v.doc_id) AS doc_id,
            COALESCE(f.fts_score, 0) AS fts_score,
            COALESCE(v.vector_score, 0) AS vector_score,
            COALESCE(f.fts_rank, -1) AS fts_rank,
            COALESCE(v.vector_rank, -1) AS vector_rank,
            -- RRF 公式：score = 1 / (rank + rank_constant)
            COALESCE(1.0 / (f.fts_rank + 60), 0) +
            COALESCE(1.0 / (v.vector_rank + 60), 0) AS final_score
        FROM fts_results f
        FULL OUTER JOIN vector_results v ON f.doc_id = v.doc_id
    )
    SELECT * FROM rrf_scores
) results
ORDER BY final_score DESC
LIMIT 10;

-- ========================================================
-- 第四部分：加权融合方法的 SQL 示例
-- ========================================================

-- 方案 2.1：平衡融合（50% 全文 + 50% 向量）
-- 用途：关键词匹配和语义相似度同等重要

WITH fts_results AS (
    SELECT 
        id,
        title,
        MATCH(content) AGAINST('artificial intelligence' IN NATURAL LANGUAGE MODE) AS fts_score
    FROM documents
    WHERE MATCH(content) AGAINST('artificial intelligence' IN NATURAL LANGUAGE MODE)
    LIMIT 100
),
vector_results AS (
    SELECT 
        id,
        1.0 / (1.0 + l2_distance(embedding, '[0.15, 0.25, ...]')) AS vector_score
    FROM documents
    ORDER BY l2_distance(embedding, '[0.15, 0.25, ...]')
    LIMIT 100
),
score_stats AS (
    SELECT 
        MAX(f.fts_score) AS max_fts,
        MIN(f.fts_score) AS min_fts,
        MAX(v.vector_score) AS max_vector,
        MIN(v.vector_score) AS min_vector
    FROM fts_results f, vector_results v
),
normalized_scores AS (
    SELECT 
        COALESCE(f.id, v.id) AS id,
        COALESCE(f.title, 'N/A') AS title,
        COALESCE(f.fts_score, 0) AS fts_score,
        COALESCE(v.vector_score, 0) AS vector_score,
        -- Min-Max 规范化
        COALESCE((f.fts_score - s.min_fts) / (s.max_fts - s.min_fts), 0) AS norm_fts,
        COALESCE((v.vector_score - s.min_vector) / (s.max_vector - s.min_vector), 0) AS norm_vector,
        s.max_fts,
        s.min_fts,
        s.max_vector,
        s.min_vector
    FROM fts_results f
    FULL OUTER JOIN vector_results v ON f.id = v.id
    CROSS JOIN score_stats s
)
SELECT 
    id,
    title,
    norm_fts,
    norm_vector,
    -- 加权和：0.5 * 规范化_fts + 0.5 * 规范化_vector
    (0.5 * norm_fts + 0.5 * norm_vector) AS final_score
FROM normalized_scores
WHERE norm_fts IS NOT NULL OR norm_vector IS NOT NULL
ORDER BY final_score DESC
LIMIT 10;

-- 方案 2.2：关键词优先融合（70% 全文 + 30% 向量）
-- 用途：用户搜索关键词通常准确，不需要太多语义理解

WITH fts_results AS (
    SELECT 
        id,
        MATCH(content) AGAINST('machine learning' IN NATURAL LANGUAGE MODE) AS fts_score
    FROM documents
    WHERE MATCH(content) AGAINST('machine learning' IN NATURAL LANGUAGE MODE)
),
vector_results AS (
    SELECT 
        id,
        1.0 / (1.0 + l2_distance(embedding, '[0.15, 0.25, ...]')) AS vector_score
    FROM documents
),
min_max_norm AS (
    SELECT 
        COALESCE(f.id, v.id) AS id,
        COALESCE(f.fts_score, 0) AS fts_score,
        COALESCE(v.vector_score, 0) AS vector_score,
        -- Min-Max 规范化
        CASE WHEN (MAX(f.fts_score) OVER () - MIN(f.fts_score) OVER ()) > 0 
             THEN (COALESCE(f.fts_score, 0) - MIN(f.fts_score) OVER ()) / 
                  (MAX(f.fts_score) OVER () - MIN(f.fts_score) OVER ())
             ELSE 0 END AS norm_fts,
        CASE WHEN (MAX(v.vector_score) OVER () - MIN(v.vector_score) OVER ()) > 0 
             THEN (COALESCE(v.vector_score, 0) - MIN(v.vector_score) OVER ()) / 
                  (MAX(v.vector_score) OVER () - MIN(v.vector_score) OVER ())
             ELSE 0 END AS norm_vector
    FROM fts_results f
    FULL OUTER JOIN vector_results v ON f.id = v.id
)
SELECT 
    id,
    -- 加权和：0.7 * 规范化_fts + 0.3 * 规范化_vector
    (0.7 * norm_fts + 0.3 * norm_vector) AS final_score
FROM min_max_norm
ORDER BY final_score DESC
LIMIT 10;

-- 方案 2.3：语义优先融合（30% 全文 + 70% 向量）
-- 用途：用户搜索意图复杂，需要通过向量搜索理解语义

WITH fts_results AS (
    SELECT 
        id,
        MATCH(content) AGAINST('neural network deep learning' IN NATURAL LANGUAGE MODE) AS fts_score
    FROM documents
    WHERE MATCH(content) AGAINST('neural network deep learning' IN NATURAL LANGUAGE MODE)
),
vector_results AS (
    SELECT 
        id,
        1.0 / (1.0 + l2_distance(embedding, '[0.15, 0.25, ...]')) AS vector_score
    FROM documents
),
weighted_hybrid AS (
    SELECT 
        COALESCE(f.id, v.id) AS id,
        COALESCE(f.fts_score, 0) AS fts_score,
        COALESCE(v.vector_score, 0) AS vector_score,
        -- Z-Score 规范化（使用 Sigmoid 函数）
        1.0 / (1.0 + EXP(-(COALESCE(f.fts_score, 0) - AVG(f.fts_score) OVER ()) / 
               STDDEV(f.fts_score) OVER ())) AS norm_fts,
        1.0 / (1.0 + EXP(-(COALESCE(v.vector_score, 0) - AVG(v.vector_score) OVER ()) / 
               STDDEV(v.vector_score) OVER ())) AS norm_vector
    FROM fts_results f
    FULL OUTER JOIN vector_results v ON f.id = v.id
)
SELECT 
    id,
    -- 加权和：0.3 * 规范化_fts + 0.7 * 规范化_vector
    (0.3 * norm_fts + 0.7 * norm_vector) AS final_score
FROM weighted_hybrid
ORDER BY final_score DESC
LIMIT 10;

-- ========================================================
-- 第五部分：高级规范化策略示例
-- ========================================================

-- 方案 3.1：Min-Max 规范化示例
-- 特点：将所有分数映射到 [0, 1] 范围内

WITH score_stats AS (
    SELECT 
        MAX(MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)) AS max_fts,
        MIN(MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)) AS min_fts,
        MAX(l2_distance(embedding, '[0.15, 0.25, ...]')) AS max_vec,
        MIN(l2_distance(embedding, '[0.15, 0.25, ...]')) AS min_vec
    FROM documents
)
SELECT 
    id,
    -- 全文搜索分数规范化
    (MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE) - s.min_fts) / 
    (s.max_fts - s.min_fts) * 0.5 +
    -- 向量搜索分数规范化（距离转相似度）
    (1.0 - (l2_distance(embedding, '[...]') - s.min_vec) / 
    (s.max_vec - s.min_vec)) * 0.5 AS final_score
FROM documents, score_stats s
WHERE MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)
ORDER BY final_score DESC
LIMIT 10;

-- 方案 3.2：Z-Score 规范化示例
-- 特点：标准化分数的分布，对异常值敏感

WITH score_stats AS (
    SELECT 
        AVG(MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)) AS avg_fts,
        STDDEV(MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)) AS std_fts,
        AVG(l2_distance(embedding, '[...]')) AS avg_vec,
        STDDEV(l2_distance(embedding, '[...]')) AS std_vec
    FROM documents
)
SELECT 
    id,
    -- 标准化分数（Z-Score）
    ((MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE) - s.avg_fts) / s.std_fts) * 0.5 +
    ((s.avg_vec - l2_distance(embedding, '[...]')) / s.std_vec) * 0.5 AS final_score
FROM documents, score_stats s
WHERE MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)
ORDER BY final_score DESC
LIMIT 10;