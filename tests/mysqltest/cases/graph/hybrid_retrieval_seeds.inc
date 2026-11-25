# Exact dense Top-2 and lexical Top-2, followed by reciprocal-rank fusion.
# The two signals are independently ranked; their raw scores are not added.
# Candidate, seed and document limits are explicit application choices, not
# fanout caps or partial-success behavior inside GRAPH_TABLE.
let $hr_seed_ctes=WITH dense_candidates AS (
 SELECT c.tenant,c.id AS chunk_id,c.entity_id,l2_distance(c.embedding,@hr_query) AS distance
 FROM hr_chunks c JOIN hr_documents d ON d.tenant=c.tenant AND d.id=c.document_id
  AND d.version=c.source_version AND d.deleted=0
 WHERE c.tenant=@hr_tenant ORDER BY distance,c.id LIMIT 2
), lexical_candidates AS (
 SELECT c.tenant,c.id AS chunk_id,c.entity_id,MATCH(c.content) AGAINST ('$hr_terms') AS relevance
 FROM hr_chunks c JOIN hr_documents d ON d.tenant=c.tenant AND d.id=c.document_id
  AND d.version=c.source_version AND d.deleted=0
 WHERE c.tenant=@hr_tenant AND MATCH(c.content) AGAINST ('$hr_terms')>0
 ORDER BY relevance DESC,c.id LIMIT 2
), ranked_channels AS (
 SELECT tenant,chunk_id,entity_id,'dense' AS channel,
  ROW_NUMBER() OVER(ORDER BY distance,chunk_id) AS signal_rank FROM dense_candidates
 UNION ALL
 SELECT tenant,chunk_id,entity_id,'lexical',
  ROW_NUMBER() OVER(ORDER BY relevance DESC,chunk_id) FROM lexical_candidates
), seeds AS (
 SELECT tenant,chunk_id,entity_id,SUM(CAST(1 AS DECIMAL(24,16))/(60+signal_rank)) AS seed_score
 FROM ranked_channels GROUP BY tenant,chunk_id,entity_id
 ORDER BY seed_score DESC,chunk_id LIMIT 3
);
