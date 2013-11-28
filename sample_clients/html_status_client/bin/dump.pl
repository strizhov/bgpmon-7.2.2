#!/usr/bin/perl

$| = 1;

use Storable qw(store retrieve freeze thaw dclone);
my $file = $ARGV[0];

my $ref = retrieve($file);
use Data::Dumper;
print Dumper($ref);

