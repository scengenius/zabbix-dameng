whenever sqlerror exit 1 rollback
set define off
set char_code UTF8
set timing off
set serveroutput on
set autocommit off

-- Dedicated fresh bootstrap test schema. All fixture changes are rolled back.
declare
  n int;
  expected_clock int;
  latest_clock int;
  displayed_name varchar(128);
begin
  insert into hosts (hostid,host,name) values (9100001,'dm-bootstrap-test','达梦a');
  select name_upper into displayed_name from hosts where hostid=9100001;
  if displayed_name<>'达梦A' then
    raise_application_error(-20001,'Host INSERT uppercase trigger failed');
  end if;
  update hosts set name='达梦b' where hostid=9100001;
  select name_upper into displayed_name from hosts where hostid=9100001;
  if displayed_name<>'达梦B' then
    raise_application_error(-20001,'Host UPDATE uppercase trigger failed');
  end if;

  insert into drules (druleid,name) values (9100002,'dm-bootstrap-test');
  insert into dchecks (dcheckid,druleid) values (9100003,9100002);
  insert into httptest (httptestid,hostid,name) values (9100004,9100001,'dm-bootstrap-test');
  insert into httpstep (httpstepid,httptestid) values (9100005,9100004);
  insert into items (itemid,hostid,key_) values (9100006,9100001,'dm.bootstrap.test');
  insert into httpstepitem (httpstepitemid,httpstepid,itemid) values (9100007,9100005,9100006);
  insert into httptestitem (httptestitemid,httptestid,itemid) values (9100008,9100004,9100006);
  insert into triggers (triggerid) values (9100009);
  insert into trigger_depends (triggerdepid,triggerid_down,triggerid_up) values (9100010,9100009,9100009);
  insert into functions (functionid,itemid,triggerid) values (9100011,9100006,9100009);
  insert into trigger_tag (triggertagid,triggerid) values (9100012,9100009);
  insert into item_preproc (item_preprocid,itemid) values (9100013,9100006);
  insert into httptest_field (httptest_fieldid,httptestid) values (9100014,9100004);
  insert into httpstep_field (httpstep_fieldid,httpstepid) values (9100015,9100005);
  insert into host_tag (hosttagid,hostid) values (9100016,9100001);
  insert into item_tag (itemtagid,itemid) values (9100017,9100006);
  insert into connector (connectorid,name) values (9100018,'dm-bootstrap-test');
  insert into connector_tag (connector_tagid,connectorid) values (9100019,9100018);
  insert into proxy_group (proxy_groupid) values (9100021);
  insert into proxy (proxyid,proxy_groupid,name) values (9100020,9100021,'dm-bootstrap-test');
  insert into host_proxy (hostproxyid,hostid,proxyid) values (9100022,9100001,9100020);
  insert into cep_rule (cep_ruleid) values (9100023);
  insert into cep_condition (cep_conditionid,cep_ruleid) values (9100024,9100023);
  insert into cep_rule_window (cep_ruleid) values (9100023);
  insert into cep_operation (cep_operationid,cep_ruleid) values (9100026,9100023);
  insert into cep_operation_condition (cep_operation_conditionid,cep_operationid) values (9100027,9100026);
  insert into cep_operation_scope_condition (cep_operation_scope_conditionid,cep_operationid) values (9100028,9100026);
  insert into services (serviceid) values (9100029);
  insert into dhosts (dhostid,druleid) values (9100030,9100002);
  insert into dservices (dserviceid,dhostid,dcheckid) values (9100031,9100030,9100003);

  update drules set druleid=druleid where druleid=9100002;
  update dchecks set dcheckid=dcheckid where dcheckid=9100003;
  update httptest set httptestid=httptestid where httptestid=9100004;
  update httpstep set httpstepid=httpstepid where httpstepid=9100005;
  update items set itemid=itemid where itemid=9100006;
  update httpstepitem set httpstepitemid=httpstepitemid where httpstepitemid=9100007;
  update httptestitem set httptestitemid=httptestitemid where httptestitemid=9100008;
  update triggers set triggerid=triggerid where triggerid=9100009;
  update trigger_depends set triggerdepid=triggerdepid where triggerdepid=9100010;
  update functions set functionid=functionid where functionid=9100011;
  update trigger_tag set triggertagid=triggertagid where triggertagid=9100012;
  update item_preproc set item_preprocid=item_preprocid where item_preprocid=9100013;
  update httptest_field set httptest_fieldid=httptest_fieldid where httptest_fieldid=9100014;
  update httpstep_field set httpstep_fieldid=httpstep_fieldid where httpstep_fieldid=9100015;
  update host_tag set hosttagid=hosttagid where hosttagid=9100016;
  update item_tag set itemtagid=itemtagid where itemtagid=9100017;
  update connector set connectorid=connectorid where connectorid=9100018;
  update connector_tag set connector_tagid=connector_tagid where connector_tagid=9100019;
  update proxy set proxyid=proxyid where proxyid=9100020;
  update proxy_group set proxy_groupid=proxy_groupid where proxy_groupid=9100021;
  update host_proxy set hostproxyid=hostproxyid where hostproxyid=9100022;
  update cep_rule set cep_ruleid=cep_ruleid where cep_ruleid=9100023;
  update cep_condition set cep_conditionid=cep_conditionid where cep_conditionid=9100024;
  update cep_rule_window set cep_ruleid=cep_ruleid where cep_ruleid=9100023;
  update cep_operation set cep_operationid=cep_operationid where cep_operationid=9100026;
  update cep_operation_condition set cep_operation_conditionid=cep_operation_conditionid where cep_operation_conditionid=9100027;
  update cep_operation_scope_condition set cep_operation_scope_conditionid=cep_operation_scope_conditionid where cep_operation_scope_conditionid=9100028;

  delete from dservices where dserviceid=9100031;
  delete from dhosts where dhostid=9100030;
  delete from services where serviceid=9100029;
  delete from cep_operation_scope_condition where cep_operation_scope_conditionid=9100028;
  delete from cep_operation_condition where cep_operation_conditionid=9100027;
  delete from cep_operation where cep_operationid=9100026;
  delete from cep_rule_window where cep_ruleid=9100023;
  delete from cep_condition where cep_conditionid=9100024;
  delete from cep_rule where cep_ruleid=9100023;
  delete from host_proxy where hostproxyid=9100022;
  delete from proxy where proxyid=9100020;
  delete from proxy_group where proxy_groupid=9100021;
  delete from connector_tag where connector_tagid=9100019;
  delete from connector where connectorid=9100018;
  delete from item_tag where itemtagid=9100017;
  delete from host_tag where hosttagid=9100016;
  delete from httpstep_field where httpstep_fieldid=9100015;
  delete from httptest_field where httptest_fieldid=9100014;
  delete from item_preproc where item_preprocid=9100013;
  delete from trigger_tag where triggertagid=9100012;
  delete from functions where functionid=9100011;
  delete from trigger_depends where triggerdepid=9100010;
  delete from triggers where triggerid=9100009;
  delete from httptestitem where httptestitemid=9100008;
  delete from httpstepitem where httpstepitemid=9100007;
  delete from items where itemid=9100006;
  delete from httpstep where httpstepid=9100005;
  delete from httptest where httptestid=9100004;
  delete from dchecks where dcheckid=9100003;
  delete from drules where druleid=9100002;
  delete from hosts where hostid=9100001;

  select count(*) into n from (
    select "OBJECT",objectid,operation from changelog
    where objectid between 9100001 and 9100031
    group by "OBJECT",objectid,operation
    having count(*)=1
  ) checked;
  if n<>84 then
    raise_application_error(-20001,'Expected 84 distinct configuration changes, got '||n);
  end if;
  select count(*) into n from (
    select "OBJECT",objectid from changelog where objectid between 9100001 and 9100031
    group by "OBJECT",objectid having count(*)=3 and min(operation)=1 and max(operation)=3
  ) checked;
  if n<>28 then
    raise_application_error(-20001,'Expected insert/update/delete for 28 distinct objects, got '||n);
  end if;
  select count(*) into n from changelog where objectid between 9100001 and 9100031;
  if n<>84 then
    raise_application_error(-20001,'Unexpected extra changelog row');
  end if;
  select count(*) into n from changelog where objectid=9100000+
    case "OBJECT"
      when 1 then 1 when 2 then 16 when 3 then 6 when 4 then 17 when 5 then 9
      when 6 then 12 when 7 then 11 when 8 then 13 when 9 then 2 when 10 then 3
      when 11 then 4 when 12 then 14 when 13 then 8 when 14 then 5 when 15 then 15
      when 16 then 7 when 17 then 18 when 18 then 19 when 19 then 20 when 20 then 21
      when 21 then 22 when 22 then 10 when 23 then 23 when 24 then 24 when 25 then 23
      when 26 then 26 when 27 then 27 when 28 then 28
    end;
  if n<>84 then
    raise_application_error(-20001,'Changelog object code or ID mismatch');
  end if;
  select max(clock) into latest_clock from changelog where objectid between 9100001 and 9100031;
  expected_clock := DATEDIFF(SECOND,TIMESTAMP '1970-01-01 00:00:00',SYS_EXTRACT_UTC(SYSTIMESTAMP));
  if abs(expected_clock-latest_clock)>5 then
    raise_application_error(-20001,'Unexpected changelog UTC epoch clock');
  end if;

  select count(*) into n from housekeeper
  where ("OBJECT"=0 and objectid=9100006) or ("OBJECT"=1 and objectid=9100009)
     or ("OBJECT"=2 and objectid=9100029) or ("OBJECT"=3 and objectid=9100030)
     or ("OBJECT"=4 and objectid=9100031);
  if n<>5 then
    raise_application_error(-20001,'Expected 5 housekeeper cleanup rows, got '||n);
  end if;
  dbms_output.put_line('Verified 84 changelog actions, 2 mixed-case host triggers and 5 housekeeping triggers.');
end;
/
rollback;

declare
  n int;
begin
  select count(*) into n from changelog where objectid between 9100001 and 9100031;
  if n<>0 then
    raise_application_error(-20001,'Changelog records persisted after rollback');
  end if;
  select count(*) into n from housekeeper where objectid between 9100001 and 9100031;
  if n<>0 then
    raise_application_error(-20001,'Housekeeper records persisted after rollback');
  end if;
  dbms_output.put_line('Rollback left zero fixture changelog and housekeeper rows.');
end;
/
exit;
