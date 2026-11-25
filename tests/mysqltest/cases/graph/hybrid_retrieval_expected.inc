# Generate expected results independently using ordinary SQL JOINs, no GRAPH_TABLE.
eval CREATE TABLE hr_expected AS WITH paths AS (
 SELECT s.tenant,s.chunk_id AS seed_chunk,s.entity_id AS seed_entity,s.seed_score,
  a.id AS endpoint,0 AS hops,CAST(NULL AS CHAR(200)) AS edges
 FROM hr_reference_seeds s JOIN hr_entities a ON a.tenant=s.tenant AND a.id=s.entity_id
 UNION ALL
 SELECT s.tenant,s.chunk_id,s.entity_id,s.seed_score,b.id,1,CAST(JSON_ARRAY(e.id) AS CHAR(200))
 FROM hr_reference_seeds s JOIN hr_entities a ON a.tenant=s.tenant AND a.id=s.entity_id
 JOIN hr_facts e ON e.tenant=a.tenant AND e.src=a.id AND e.kind='depends_on'
  AND e.valid_from<=@hr_asof AND (e.valid_to IS NULL OR e.valid_to>@hr_asof)
 JOIN hr_entities b ON b.tenant=e.tenant AND b.id=e.dst
 UNION ALL
 SELECT s.tenant,s.chunk_id,s.entity_id,s.seed_score,c.id,2,CAST(JSON_ARRAY(e.id,f.id) AS CHAR(200))
 FROM hr_reference_seeds s JOIN hr_entities a ON a.tenant=s.tenant AND a.id=s.entity_id
 JOIN hr_facts e ON e.tenant=a.tenant AND e.src=a.id AND e.kind='depends_on'
  AND e.valid_from<=@hr_asof AND (e.valid_to IS NULL OR e.valid_to>@hr_asof)
 JOIN hr_entities b ON b.tenant=e.tenant AND b.id=e.dst
 JOIN hr_facts f ON f.tenant=b.tenant AND f.src=b.id AND f.kind='depends_on'
  AND f.valid_from<=@hr_asof AND (f.valid_to IS NULL OR f.valid_to>@hr_asof)
 JOIN hr_entities c ON c.tenant=f.tenant AND c.id=f.dst
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
