/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Hybrid Search Implementation Guide and SQL Examples
 * ================================
 *
 * This document provides detailed instructions and SQL examples on how to use hybrid search features.
 */

-- ========================================================
-- Part 1: Table Structure Design
-- ========================================================

-- Create a table with vector and full-text indexes
CREATE TABLE documents (
    id INT PRIMARY KEY,
    title VARCHAR(255),
    content TEXT,
    embedding VECTOR(384),  -- 384-dimensional vector
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    
    -- Full-text index configuration
    FULLTEXT INDEX idx_content(content) WITH PARSER jieba,
    
    -- Vector index configuration
    -- DISTANCE=l2: Uses L2 Euclidean distance
    -- TYPE=hnsw: Uses HNSW (Hierarchical Navigable Small Worlds) algorithm
    -- LIB=vsag: Uses VSAG vector search library
    VECTOR INDEX idx_embedding(embedding) WITH(DISTANCE=l2, TYPE=hnsw, LIB=vsag)
) ORGANIZATION = HEAP;

-- ========================================================
-- Part 2: Data Insertion Example
-- ========================================================

-- Insert sample data
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
-- Part 3: SQL Examples for RRF Fusion Method
-- ========================================================

-- Scheme 1.1: Basic RRF Fusion Query
-- Use Case: Automatic normalization needed, robust to outliers
-- Parameter Explanation:
--   rank_constant: 60 (larger values are more favorable to low-ranked documents)
--   rank_window_size: 100 (fuse from 100 results)

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
            -- RRF formula: score = 1 / (rank + rank_constant)
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
-- Part 4: SQL Examples for Weighted Fusion Method
-- ========================================================

-- Scheme 2.1: Balanced Fusion (50% Full-text + 50% Vector)
-- Use Case: Keyword matching and semantic similarity are equally important

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
        -- Min-Max normalization
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
    -- Weighted sum: 0.5 * normalized_fts + 0.5 * normalized_vector
    (0.5 * norm_fts + 0.5 * norm_vector) AS final_score
FROM normalized_scores
WHERE norm_fts IS NOT NULL OR norm_vector IS NOT NULL
ORDER BY final_score DESC
LIMIT 10;

-- Scheme 2.2: Keyword Priority Fusion (70% Full-text + 30% Vector)
-- Use Case: Users' search keywords are usually accurate, minimal semantic understanding needed

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
        -- Min-Max normalization
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
    -- Weighted sum: 0.7 * normalized_fts + 0.3 * normalized_vector
    (0.7 * norm_fts + 0.3 * norm_vector) AS final_score
FROM min_max_norm
ORDER BY final_score DESC
LIMIT 10;

-- Scheme 2.3: Semantic Priority Fusion (30% Full-text + 70% Vector)
-- Use Case: Complex user search intent, need to understand semantics through vector search

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
        -- Z-Score normalization (using Sigmoid function)
        1.0 / (1.0 + EXP(-(COALESCE(f.fts_score, 0) - AVG(f.fts_score) OVER ()) / 
               STDDEV(f.fts_score) OVER ())) AS norm_fts,
        1.0 / (1.0 + EXP(-(COALESCE(v.vector_score, 0) - AVG(v.vector_score) OVER ()) / 
               STDDEV(v.vector_score) OVER ())) AS norm_vector
    FROM fts_results f
    FULL OUTER JOIN vector_results v ON f.id = v.id
)
SELECT 
    id,
    -- Weighted sum: 0.3 * normalized_fts + 0.7 * normalized_vector
    (0.3 * norm_fts + 0.7 * norm_vector) AS final_score
FROM weighted_hybrid
ORDER BY final_score DESC
LIMIT 10;

-- ========================================================
-- Part 5: Advanced Normalization Strategy Examples
-- ========================================================

-- Scheme 3.1: Min-Max Normalization Example
-- Characteristic: Maps all scores to [0, 1] range

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
    -- Full-text search score normalization
    (MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE) - s.min_fts) / 
    (s.max_fts - s.min_fts) * 0.5 +
    -- Vector search score normalization (distance to similarity)
    (1.0 - (l2_distance(embedding, '[...]') - s.min_vec) / 
    (s.max_vec - s.min_vec)) * 0.5 AS final_score
FROM documents, score_stats s
WHERE MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)
ORDER BY final_score DESC
LIMIT 10;

-- Scheme 3.2: Z-Score Normalization Example
-- Characteristic: Standardizes score distribution, sensitive to outliers

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
    -- Standardized scores (Z-Score)
    ((MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE) - s.avg_fts) / s.std_fts) * 0.5 +
    ((s.avg_vec - l2_distance(embedding, '[...]')) / s.std_vec) * 0.5 AS final_score
FROM documents, score_stats s
WHERE MATCH(content) AGAINST('query' IN NATURAL LANGUAGE MODE)
ORDER BY final_score DESC
LIMIT 10;
