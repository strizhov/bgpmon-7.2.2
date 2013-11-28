#!/usr/bin/perl

$| = 1;

use lib qw(./lib ./conf);
use FindBin qw($Bin);
use File::Basename;
use File::Path;
use Getopt::Long;
use Storable qw(store retrieve freeze thaw dclone);

require 'util.pl';

my $result = GetOptions(
                         "peer=s" => \$_PEER_ADDR,  # port
                       );
my $file = $ARGV[0];
my $filter = {
                addr => $_PEER_ADDR,
             };

my $ref = retrieve($file);
use Data::Dumper;
print_header();
if (ref $ref eq 'HASH')
{
    my $timestampe   = $ref->{TIMESTAMP};
    my $sessions_ref = filter_session($filter, $ref->{DATA});
    print_sessions($timestampe, $sessions_ref);
}
elsif ( ref $ref eq 'ARRAY')
{
	foreach my $entry ( @$ref )
	{
		#print Dumper($entry);
        my $timestampe   = $entry->{TIMESTAMP};
        my $sessions_ref = filter_session($filter, $entry->{DATA});
        #print Dumper($timestampe, $sessions_ref);
        print_sessions($timestampe, $sessions_ref);
	}
}

sub filter_session
{
    my ($filter, $sessions_ref) = @_;
    my @filtered_sessions = ();

    foreach my $session ( @$sessions_ref )
    {
        if ( ( (not $filter->{addr}) or $session->{addr} eq $filter->{addr}))
        {
            push @filtered_sessions, $session; 
        }
    }
    return \@filtered_sessions;
}

sub print_line
{
    printf("%-12.12s %-32.32s %10.10s %10.10s %10.10s %10.10s %10.10s %10.10s\n", @_);
}

sub print_header
{
    print_line(
                "TIMESTAMP",
                "ADDR",
                "ANNO",
                "DANNO",
                "SPATH",
                "DPATH",
                "WITH",
                "DWITH",
              );
    printf("%s\n", "-" x 120);
}

sub print_sessions
{
    my ($timestampe, $sessions_ref) = @_;
    #print Dumper($sessions_ref);


    foreach my $session ( @$sessions_ref )
    {
        print_line(
                          $timestampe,
                          $session->{addr},
                          $session->{anno}{current},
                          $session->{dup_anno}{current},
                          $session->{same_path}{current},
                          $session->{diff_path}{current},
                          $session->{withdrawal}{current},
                          $session->{dup_withdrawal}{current},
                  );
    }
}

# vim: sw=4 ts=4 sts=4 expandtab
