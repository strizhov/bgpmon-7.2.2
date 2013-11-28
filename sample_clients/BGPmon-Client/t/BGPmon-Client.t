# Before `make install' is performed this script should be runnable with
# `make test'. After `make install' it should work as `perl BGPmon-Client.t'

#########################

# change 'tests => 1' to 'tests => last_test_to_print';

use Test::More;
BEGIN { use_ok('BGPmon::Client') };
use_ok('BGPmon::Client');
#########################

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.

my $xml_string;
sub process_func{

  my $data_string = shift;
  if(!defined($data_string)){
    return 0;
  }
  $xml_string .= $data_string;
  return 1;
}

# Insert your test code below, the Test::More module is use()ed here so read
# its man page ( perldoc Test::More ) for help writing this test script.
my $client = BGPmon::Client->new();
$client->address('127.0.0.1');
$client->port(50003);
$client->xml_handler(\&process_func);

ok($client->address() eq '127.0.0.1',"Set Addr");
ok($client->port() == 50003, "Set Port");
ok($client->connect() == 0, "Connect, server not yet up");
ok($client->error_count() == 1,"ERROR: " . $client->error());
ok($client->run(10) == 0,"Running: No Server"); ## run for ten seconds
ok($client->error_count() == 2 ,"ERROR: " . $client->error()); 
$client->clear_errors();

ok($client->port(50002) == 50002, "Set Port");
ok($client->error_count() == 0,"error count should be 0");
if($client->error_count() > 0){
  if(defined($client->error())){
    diag("error is " . $client->error());
  }else{
    diag("error is undef");
  }
}
ok($client->connect(), "Connect");
ok($client->error_count() == 0,"error count should be 0");
if($client->error_count() > 0){
  if(defined($client->error())){
    diag("error is " . $client->error());
  }else{
    diag("error is undef");
  }
}
ok($client->run(10),"Running "); ## run for ten seconds
ok($client->error_count() == 0,"error cound should be 0");
if($client->error_count() > 0){
  if(defined($client->error())){
    diag("error is " . $client->error());
  }else{
    diag("error is undef");
  }
}
ok($xml_string =~ /xml/,"String was obtained: $xml_string");

done_testing();


