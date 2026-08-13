# owner: gaishun.gs
# connection conn_obtest_z1;
# mysql mode

connect (conn_obtest_z1_mix1,$ipz1,root@sys,,db1,$portz1);
connect (conn_obtest_z1_mix2,$ipz1,root@sys,,db1,$portz1);

connection conn_obtest_z1_mix1;
set autocommit=false;

start transaction;
insert into rename_table.t_mix(i, i_i, lob_t) values (1, 1, 'for test in mix');
connection conn_obtest_z1_mix2;
rename table rename_table.t_mix to rename_table.t_mix_new;
connection conn_obtest_z1_mix1;
--error 1146
insert into rename_table.t_mix(i, i_i, lob_t) values (2, 2, 'for test in mix');
insert into rename_table.t_mix_new(i, i_i, lob_t) values (3, 3, 'for test in mix');
connection conn_obtest_z1_mix2;
alter table rename_table.t_mix_new rename to rename_table.t_mix_new2;
connection conn_obtest_z1_mix1;
--error 1146
insert into rename_table.t_mix_new(i, i_i, lob_t) values (4, 4, 'for test in mix');
insert into rename_table.t_mix_new2(i, i_i, lob_t) values (5, 5, 'for test in mix');
select * from rename_table.t_mix_new2;
commit;
