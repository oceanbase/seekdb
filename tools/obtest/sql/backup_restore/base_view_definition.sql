CREATE TABLE base_view_table (
   col1 int primary key,
   col2 varchar(30),
   col3 smallint
);
INSERT INTO base_view_table values (1,"First Entry",1);
INSERT INTO base_view_table values (2,"Second Entry",2);
INSERT INTO base_view_table values (3,"Third Entry",3);


CREATE VIEW base_view_1 AS SELECT base_view_table.col1 AS column1, base_view_table.col2 AS column2, base_view_table.col3 AS column3 from base_view_table;

CREATE VIEW base_view_2 (view2_column1,view2_column2,view2_column3) AS SELECT col1,col2,col3 from base_view_table;

CREATE VIEW base_view_ghost AS SELECT base_view_table.col1 AS column1, base_view_table.col2 AS column2, base_view_table.col3 AS column3 from base_view_table;

DROP VIEW base_view_ghost;
