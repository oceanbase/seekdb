-- SQL/PGQ target syntax, documentation fixtures, NOT accepted by seekdb yet.
-- Uses the relational tables in graph_p0/fixture.sql. Oracle-style IS labels.
CREATE PROPERTY GRAPH p0_graph
VERTEX TABLES (
  gp_vertex KEY (tenant,id) LABEL person PROPERTIES (tenant,id,name),
  gp_company KEY (tenant,id) LABEL company PROPERTIES (tenant,id,name)
)
EDGE TABLES (
  gp_edge KEY (tenant,id)
    SOURCE KEY (src_tenant,src) REFERENCES gp_vertex (tenant,id)
    DESTINATION KEY (dst_tenant,dst) REFERENCES gp_vertex (tenant,id)
    LABEL knows PROPERTIES (tenant,id,weight),
  gp_works KEY (tenant,id)
    SOURCE KEY (src_tenant,src) REFERENCES gp_vertex (tenant,id)
    DESTINATION KEY (dst_tenant,dst) REFERENCES gp_company (tenant,id)
    LABEL works PROPERTIES (tenant,id)
);

-- out: 7 matches on fixture; no DISTINCT.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person)
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id));

-- in: 7 matches; endpoint binding reverses, stored edge identity does not.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)<-[e IS knows]-(b IS person)
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id));

-- filtered: 6 matches.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person)
  WHERE e.weight=1
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id));

-- chain: 13 matches with repeatable elements in the P0 profile.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person)-[f IS knows]->(c IS person)
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id,f.tenant AS f_tenant,f.id AS f_id,c.tenant AS c_tenant,c.id AS c_id));

-- trail protocol example: 11 matches. Explicit identity inequality, NOT a claim of TRAIL syntax support.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person)-[f IS knows]->(c IS person)
  WHERE e.tenant<>f.tenant OR e.id<>f.id
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id,f.tenant AS f_tenant,f.id AS f_id,c.tenant AS c_tenant,c.id AS c_id));

-- converge: 27 matches; shared b is one vertex binding.
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS knows]->(b IS person), (c IS person)-[f IS knows]->(b)
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id,f.tenant AS f_tenant,f.id AS f_id,c.tenant AS c_tenant,c.id AS c_id));

-- works: 1 match; gp_company(1,1) differs from gp_vertex(1,1).
SELECT * FROM GRAPH_TABLE(p0_graph
  MATCH (a IS person)-[e IS works]->(b IS company)
  COLUMNS(a.tenant AS a_tenant,a.id AS a_id,e.tenant AS e_tenant,e.id AS e_id,b.tenant AS b_tenant,b.id AS b_id));

-- Negative fixtures for the future Parser/Resolver; not executed by this P0 tool:
-- SELECT * FROM GQL_TABLE(p0_graph MATCH (a) COLUMNS(a.id)); -- unsupported entrypoint
-- SELECT * FROM GRAPH_TABLE(p0_graph MATCH (a IS absent) COLUMNS(a.id)); -- unknown label
-- SELECT * FROM GRAPH_TABLE(p0_graph MATCH (a IS person) COLUMNS(a.absent)); -- unknown property
-- SELECT * FROM GRAPH_TABLE(p0_graph MATCH (a IS person)-[a IS knows]->(b) COLUMNS(b.id)); -- variable kind conflict
