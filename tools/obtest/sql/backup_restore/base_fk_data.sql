set foreign_key_checks=on;
drop table if exists t11, t1;

create table t1(pk int, a int, b int, primary key (pk), unique key uk_a_b (a, b));
create table t11(pk int, a int, b int, primary key (pk), unique key uk_a_b (a, b),
                      constraint foreign key (a, b) references t1 (a, b) on update CASCADE on delete CASCADE);

insert into t1 values (10, 11, 12), (20, 21, 22), (30, 31, 32), (40, 41, 42);
insert into t11 values (10, 11, 12), (20, 21, 22), (30, 31, 32), (40, 41, 42);
