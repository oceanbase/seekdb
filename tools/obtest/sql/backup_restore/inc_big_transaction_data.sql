create table big_transaction_test(pk int primary key, a int, b int);

let $inc_count = 1;
while ($inc_count < 201)
{
   eval insert into big_transaction_test values ($inc_count ,1 ,1);
   inc $inc_count;
}

update big_transaction_test set pk = pk * 1000;
delete from big_transaction_test where pk = 1000;
begin;
let $inc_count = 1;
while ($inc_count < 201)
{
   insert into big_transaction_test values (1000 ,1 ,1);
   delete from big_transaction_test where pk = 1000;
   inc $inc_count;
}
commit;
insert into big_transaction_test values (1000 ,1 ,1);

