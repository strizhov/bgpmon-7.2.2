#!/usr/bin/perl

use strict;
use BGPmon::Client;
use BGPmon::Client::ParseTable;
use Getopt::Long;
require XML::Simple;

$| = 1;

my $server_addr = "127.0.0.1";
my $server_port = 50002;
my $filename = "bgp_table_status.txt";

my $result = GetOptions( "server=s" => \$server_addr,  # server 
                         "port=i"   => \$server_port,  # port
                         "file=s"   => \$filename,
                       );
die "Unable to parse command line arguments\n" unless $result;

my $client = BGPmon::Client->new();
my $handler = BGPmon::Client::ParseTable->new();
$handler->output_file("test_output.txt");
$client->address($server_addr);
$client->port($server_port);
$client->xml_handler(\&BGPmon::Client::ParseTable::summarize_tables);
$client->xml_handler_params($handler);
die "Unable to connect to server at $server_addr:$server_port\n" unless $client->connect();
die "Error during execution\n" unless $client->run();

