create user inc_test_user_1 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user inc_test_user_2 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user inc_test_user_3  IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
create user inc_test_user_4 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user inc_test_user_5 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user inc_test_user_6 IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
create user inc_test_user_7 IDENTIFIED BY PASSWORD '*4acfe3202a5ff5cf467898fc58aab1d615029441';
create user inc_test_user_8 IDENTIFIED BY PASSWORD '*e9c2bcdc178a99b7b08dd25db58ded2ee5bff050';
create user inc_test_user_9 IDENTIFIED BY PASSWORD '*1975d095ac033caf4e1bf94f7202a9bbfeeb66f1';
drop user inc_test_user_8;

GRANT DROP ON `test`.* TO 'inc_test_user_2';
GRANT ALTER ON `mysql`.* TO 'inc_test_user_8';
GRANT CREATE ON `mysql`.* TO 'inc_test_user_5';
GRANT UPDATE ON `mysql`.* TO 'inc_test_user_3';
GRANT DELETE ON `test`.* TO 'inc_test_user_6';
GRANT INSERT ON `mysql`.* TO 'inc_test_user_7';
GRANT ALL PRIVILEGES ON *.* TO 'inc_test_user_1' WITH GRANT OPTION;
GRANT SELECT ON `oceanbase`.* TO 'inc_test_user_5';
GRANT INDEX ON `mysql`.* TO 'inc_test_user_4';
GRANT CREATE VIEW ON `mysql`.* TO 'inc_test_user_7';
GRANT SHOW VIEW ON `test`.* TO 'inc_test_user_6';
GRANT select ON `oceanbase`.* TO 'inc_test_user_4';

REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'inc_test_user_1';
REVOKE UPDATE ON `mysql`.* FROM 'inc_test_user_3';
REVOKE SELECT ON `oceanbase`.* FROM 'inc_test_user_4';	








