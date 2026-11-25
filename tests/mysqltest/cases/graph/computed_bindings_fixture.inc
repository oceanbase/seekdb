CREATE TABLE cb_vertex(id BIGINT PRIMARY KEY);
CREATE TABLE cb_edge(id BIGINT PRIMARY KEY,src BIGINT,dst BIGINT,since BIGINT,kind VARCHAR(20));
CREATE TABLE cb_seed(id INT PRIMARY KEY,seed BIGINT,cutoff BIGINT,delta BIGINT,kind VARCHAR(20));
CREATE TABLE cb_reference(id INT PRIMARY KEY,seed BIGINT,cutoff BIGINT,kind VARCHAR(20));
INSERT INTO cb_vertex VALUES(1),(2),(3);
INSERT INTO cb_edge VALUES(10,1,2,2020,'KNOWS'),(11,1,2,2022,'knows'),
 (12,2,3,2023,'KNOWS'),(13,2,1,2021,'likes'),(14,1,3,2024,'LIKES'),
 (15,1,99,2024,'knows'),(16,1,3,2024,NULL);
INSERT INTO cb_seed VALUES(1,1,-2020,0,'KnOwS'),(2,1,-2022,0,'KNOWS'),
 (3,1,-2020,0,'KnOwS'),(4,1,NULL,0,'knows'),(5,1,-2020,1,'knows'),
 (6,1,-2020,0,'LiKeS'),(7,1,-2020,0,NULL),(8,1,-2020,0,''),
 (9,5,-2020,0,'knows'),(10,1,-2020,2,'KNOWS'),(11,1,-2020,0,'');
# Independent normalized bindings, not a copy of the expressions under test.
INSERT INTO cb_reference VALUES(1,1,2020,'knows'),(2,1,2022,'knows'),
 (3,1,2020,'knows'),(4,1,2022,'knows'),(5,1,2021,'knows'),
 (6,1,2020,'likes'),(7,1,2020,'knows'),(8,1,2020,''),
 (9,5,2020,'knows'),(10,1,2022,'knows'),(11,1,2020,'');
CREATE PROPERTY GRAPH cb_graph
 VERTEX TABLES(cb_vertex KEY(id) LABEL person PROPERTIES(id))
 EDGE TABLES(cb_edge KEY(id) SOURCE KEY(src) REFERENCES cb_vertex(id)
 DESTINATION KEY(dst) REFERENCES cb_vertex(id)
 LABEL link PROPERTIES(id,since,kind));
CREATE TABLE cb_expected AS
 SELECT s.id,a.id AS endpoint,0 AS hops,CAST(NULL AS CHAR(100)) AS edges
 FROM cb_reference s JOIN cb_vertex a ON a.id=s.seed
 UNION ALL
 SELECT s.id,b.id,1,CAST(JSON_ARRAY(e.id) AS CHAR(100))
 FROM cb_reference s JOIN cb_vertex a ON a.id=s.seed
 JOIN cb_edge e ON e.src=a.id AND e.since>=s.cutoff AND e.kind=s.kind
 JOIN cb_vertex b ON b.id=e.dst
 UNION ALL
 SELECT s.id,c.id,2,CAST(JSON_ARRAY(e.id,f.id) AS CHAR(100))
 FROM cb_reference s JOIN cb_vertex a ON a.id=s.seed
 JOIN cb_edge e ON e.src=a.id AND e.since>=s.cutoff AND e.kind=s.kind
 JOIN cb_vertex b ON b.id=e.dst
 JOIN cb_edge f ON f.src=b.id AND f.since>=s.cutoff AND f.kind=s.kind
 JOIN cb_vertex c ON c.id=f.dst;
let $cb_query=SELECT s.id,g.endpoint,g.hops,CAST(g.edges AS CHAR(100)) AS edges
 FROM cb_seed s,LATERAL (
 SELECT * FROM GRAPH_TABLE(cb_graph MATCH
 (a IS person WHERE a.id=s.seed)
 -[e IS link WHERE e.since>=COALESCE(ABS(s.cutoff)+s.delta,2022)
 AND LOWER(e.kind)=LOWER(COALESCE(s.kind,'knows'))
 AND LENGTH(UPPER(e.kind))>0]->{0,2}(b IS person)
 COLUMNS(b.id AS endpoint,COUNT(e.id) AS hops,JSON_ARRAYAGG(e.id) AS edges)) p) g;
let $cb_derived=SELECT s.id,g.endpoint,g.hops,CAST(g.edges AS CHAR(100)) AS edges
 FROM (SELECT id,seed,COALESCE(ABS(cutoff)+delta,2022) AS threshold,
 LOWER(COALESCE(kind,'knows')) AS category FROM cb_seed) s,LATERAL (
 SELECT * FROM GRAPH_TABLE(cb_graph MATCH
 (a IS person WHERE a.id=s.seed)
 -[e IS link WHERE e.since>=s.threshold AND LOWER(e.kind)=s.category]->{0,2}(b IS person)
 COLUMNS(b.id AS endpoint,COUNT(e.id) AS hops,JSON_ARRAYAGG(e.id) AS edges)) p) g;
