# Materialize the final evidence only for multiset comparison. The candidate
# retrieval, graph expansion and document ranking below are one SQL statement.
# MAX per seed/document prevents parallel paths from inflating document rank;
# joining the selected documents back to evidence preserves every path witness.
eval CREATE TABLE hr_actual AS $hr_seed_ctes,
paths AS (
 SELECT s.tenant,s.chunk_id AS seed_chunk,s.entity_id AS seed_entity,s.seed_score,
  g.endpoint,g.hops,CAST(g.edges AS CHAR(200)) AS edges
 FROM seeds s,LATERAL (
  SELECT * FROM GRAPH_TABLE(hr_graph MATCH
   (a IS entity WHERE a.tenant=s.tenant AND a.id=s.entity_id)
   -[e IS fact WHERE e.tenant=s.tenant AND e.kind='depends_on'
     AND e.valid_from<=@hr_asof AND (e.valid_to IS NULL OR e.valid_to>@hr_asof)]->{0,2}
   (b IS entity)
   COLUMNS(b.id AS endpoint,COUNT(e.id) AS hops,JSON_ARRAYAGG(e.id) AS edges)) p
 ) g
), evidence AS (
 SELECT p.*,c.id AS chunk_id,c.document_id,c.source_version
 FROM paths p JOIN hr_chunks c ON c.tenant=p.tenant AND c.entity_id=p.endpoint
 JOIN hr_documents d ON d.tenant=c.tenant AND d.id=c.document_id
  AND d.version=c.source_version AND d.deleted=0
), per_seed_document AS (
 SELECT tenant,document_id,source_version,seed_chunk,
  MAX(seed_score/(1+hops)) AS contribution FROM evidence
 GROUP BY tenant,document_id,source_version,seed_chunk
), selected_documents AS (
 SELECT tenant,document_id,source_version,SUM(contribution) AS document_score
 FROM per_seed_document GROUP BY tenant,document_id,source_version
 ORDER BY document_score DESC,document_id,source_version LIMIT $hr_document_limit
)
SELECT e.tenant,e.seed_chunk,e.seed_entity,CAST(e.seed_score AS DECIMAL(18,12)) AS seed_score,
 e.endpoint,e.hops,e.edges,e.chunk_id,e.document_id,e.source_version,d.source_uri,
 CAST(w.document_score AS DECIMAL(18,12)) AS document_score
FROM selected_documents w JOIN evidence e ON e.tenant=w.tenant AND e.document_id=w.document_id
 AND e.source_version=w.source_version
JOIN hr_documents d ON d.tenant=w.tenant AND d.id=w.document_id AND d.version=w.source_version;
