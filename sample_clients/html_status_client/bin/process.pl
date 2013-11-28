#!/usr/bin/perl
use FindBin qw($Bin);

chdir $Bin;

#Directory
my $WEBHTMLDIR  = "~/public_html/";
my $TOOLSDIR    = "../data/";
my $SERVER_ADDR = "129.82.138.6";
my $SERVER_PORT = 50001;

print "./stat_client.pl -D -k keepalive --server=$SERVER_ADDR --port=$SERVER_PORT >> stat_client.log\n";
system("./stat_client.pl -D -k keepalive --server=$SERVER_ADDR --port=$SERVER_PORT >> stat_client.log");
print "./stat_web_gen.pl $WEBHTMLDIR $TOOLSDIR >> stat_web_gen.log\n";
system("./stat_web_gen.pl $WEBHTMLDIR $TOOLSDIR >> stat_web_gen.log");
