connection conn_mysql;
create database rename_table;
create table rename_table.t_mix (i int, i_i int, lob_t text);
create table rename_table.t_finish (i int, i_i int, lob_t text);
 
create index idx_t_mix_i_i on rename_table.t_mix(i_i);
create index idx_t_finish_i_i on rename_table.t_finish(i_i);
 
insert into rename_table.t_mix(i, i_i, lob_t) values (1, 1, 'for test'), (1, 1, 'for test');
insert into rename_table.t_finish(i, i_i, lob_t) values (1, 1, 'for test'), (1, 1, 'for test');