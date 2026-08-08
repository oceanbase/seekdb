CREATE TABLE inc_view_table (
   col1 int primary key,
   col2 varchar(30),
   col3 smallint
);
INSERT INTO inc_view_table values (1,"First Entry",1);
INSERT INTO inc_view_table values (2,"Second Entry",2);
INSERT INTO inc_view_table values (3,"Third Entry",3);


CREATE VIEW inc_view_1 AS SELECT inc_view_table.col1 AS column1, inc_view_table.col2 AS column2, inc_view_table.col3 AS column3 from inc_view_table;

CREATE VIEW inc_view_2 (view2_column1,view2_column2,view2_column3) AS SELECT col1,col2,col3 from inc_view_table;

CREATE VIEW inc_view_ghost AS SELECT inc_view_table.col1 AS column1, inc_view_table.col2 AS column2, inc_view_table.col3 AS column3 from inc_view_table;

DROP VIEW inc_view_ghost;
