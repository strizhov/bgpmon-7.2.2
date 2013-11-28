#!/usr/bin/perl

$| = 1;

use strict;
use lib qw(../lib);
use FindBin qw($Bin);
use File::Basename;
use File::Path;
use Getopt::Long;
use IO::Select;
use IO::Socket;
use Storable qw(store retrieve freeze thaw dclone);
use POSIX 'setsid';

require 'util.pl';

###
### Initialize global system constants
###
my $_SOCKET_TIMEOUT  = 10 * 60;  # 10 minutes timeout
my $_SOCKET_READLEN  = 2 * 512;      # 512 characters per read
my $_MAX_BUFFER_SIZE = 3  * 1024 * 1024; # 2M
my $_STATUS_INTERVAL = 1  * 10;  # 10 seconds status report interval
#my $_OUTPUT_INTERVAL = 5  * 60;  # 1 minutes output interval
my $_OUTPUT_INTERVAL = 60;  # 1 minutes output interval
my $_OUTPUT_DIR      = "../data";

###
### Internal variables and data buffer
###
my $_LAST_STATUS_TIME = time;
my $_LAST_OUTPUT_TIME = time;
my %_SOCKET_BUFFER    = ();
my @_XML_BUFFER       = ();
my @_STAT_BUFFER      = ();
my $_WINDOW_SIZE      = 48 * 60 / 5;

###
### Initialize global system variables
###
chdir $Bin;
my $_LOGS_PATH = ".";
my $_PROG_NAME = "$0";
my $_LOG_NAME  = "$_PROG_NAME.log";

###
### Initialize global user variables
###
my $_ACTION       = 'start';
my $_DAEMON       = 0;
my $_SERVER_ADDR  = '127.0.0.1'; 
my $_SERVER_PORT  = '50001'; 

###
### Get options
###
my $result = GetOptions(
                         "k=s" => \$_ACTION,  # action
                         "D"   => \$_DAEMON,  # daemon
                         "server=s" => \$_SERVER_ADDR,  # port
                         "port=i"   => \$_SERVER_PORT,  # port
                       );
die "Unable to parse command line arguments\n" unless $result;
print "SERVER: $_SERVER_ADDR\n";
print "PORT: $_SERVER_PORT\n";

###
### [TODO] Check required modules and external programs
###
#if (! -f $_EXEC_BZIP2)
#{
#    plog(sprintf("%s", "[init][error] bzip2 is not installed")); # Log the error
#    die "\n";
#}
if ($_OUTPUT_INTERVAL < 60)
{
    #plog(sprintf("%s", "[init][warning] Output interval is shorter than 60 seconds. Output files would be overwritten")); 
}

###
### Load current process ids
###
my @pids;
if(open(PID, "$_LOGS_PATH/$_PROG_NAME.pid")) {
    while(<PID>) {
        chomp;
        push @pids, $_;
    }
    close(PID);
}

###
### Perform actions
###
if ( $_ACTION =~ /^stop$/i ) 
{ # stop
    if(scalar @pids) 
    {
        foreach(@pids) 
        {
            print "Kill Process ($_) with SIGKILL\n";
            kill 9, $_;
        }
    } 
    else { print STDERR "$_PROG_NAME is not running\n"; }
    unlink("$_LOGS_PATH/$_PROG_NAME.pid");
    exit;
}
elsif ( $_ACTION =~ /^status$/i ) 
{ # status
    if(scalar @pids) { print "$_PROG_NAME is running (" . join(' ', @pids) . ")\n"; } 
    else             { print "$_PROG_NAME is not running\n";                        }
    exit;
}
elsif ( $_ACTION =~ /^keepalive$/i ) 
{ # status
    if(scalar @pids) { print "$_PROG_NAME is running (" . join(' ', @pids) . ")\n"; exit;} 
    else             { print "$_PROG_NAME is not running\n";                             }
}
else
{
    unlink("$_LOGS_PATH/$_PROG_NAME.pid");
}

foreach(@pids) 
{ # Stop current processes
    kill 9, $_;
}
undef @pids;

$SIG{CHLD}='IGNORE';

if ($_DAEMON)
{ # Background daemon
    if(!fork) 
    { # background Daemon
        # close standard in/out
        close(STDIN);
        close(STDOUT);
        close(STDERR);
        setsid();
        plog("$_PROG_NAME: daemon started");
        log_pid("$_PROG_NAME");
        main();
    } 
    else 
    { # exit my self
        exit(0);
    }
}
else
{ # Foreground process
    main();
}

exit;


### =====================================================================
### Function
### =====================================================================
sub log_pid {
    open(PID, ">>$_LOGS_PATH/$_[0].pid");
    print PID $$, "\n";
    close(PID);
}

sub plog {
    my @t = localtime(time);
    my $time = sprintf("[%4d-%02d-%02d %02d:%02d:%02d] ", $t[5]+1900, $t[4]+1, $t[3], $t[2], $t[1], $t[0]);
    open(LOG, ">>$_LOGS_PATH/$_LOG_NAME");
    print LOG $time, @_, "\n";
    print $time, @_, "\n";
    close(LOG);
}


sub main {
    plog("[main]");
    
    #---------------------------------------------------------------
    # Infinite loop
    #---------------------------------------------------------------
    use bigint;

    for (my $i=1; ;$i++)
    {
        my ($sock, $sel, @ready);
        eval
        {

            plog(sprintf("%-75.75s", "[main][loop:$i]" . "=" x 100));

            my $_INTERVAL_UPDATE_DBH        = 60;
            my $_INTERVAL_UPDATE_MGT_SERVER = 15*60;
            my $_INTERVAL_UPDATE_HN_IP      = 60*60;
            #-------------------------------------------------------
            # Connect to server
            #-------------------------------------------------------
            $sock = new IO::Socket::INET ( PeerAddr  => $_SERVER_ADDR,
                                           PeerPort  => $_SERVER_PORT,
                                           Proto => 'tcp',
                                         ) or die "Could not create socket to $_SERVER_ADDR:$_SERVER_PORT: $!\n";
            $sel  = new IO::Select($sock);

            #-------------------------------------------------------
            # Receive xml stream
            #-------------------------------------------------------
            # Loop with timeout 
            while(@ready = $sel->can_read($_SOCKET_TIMEOUT)) 
            {
                #-----------------------------
                # Read from each ready socket
                #-----------------------------
                foreach my $fh (@ready) 
                {
                    my $text      = "";
                    my $xml_line  = "";
                    my $stat_data = "";
            
                    # There are three possible return values of sysread
                    # http://perldoc.perl.org/functions/sysread.html
                    my $readlen = sysread($fh, $text, $_SOCKET_READLEN);

                    if ($readlen > 0)
                    { # (1) normal 
                        $_SOCKET_BUFFER{$fh} .= $text; 
                    }
                    elsif ($readlen == 0)
                    { # (2) end of socket

                        # Raise exception
                        die "Socket closed by server $_SERVER_ADDR\n";
                    }
                    else
                    { # (3) error
                        
                        # Raise exception
                        die "Socket error $!\n";
                    }

                    # Safety check
                    if (length($_SOCKET_BUFFER{$fh}) > $_MAX_BUFFER_SIZE)
                    {
                        die "Message size too large\n";
                    }

                    # Filter the open tab <xml> or <BGP_MESSAGES>
                    if ( $_SOCKET_BUFFER{$fh} =~ /^\s*(<xml>|<BGP_MESSAGES>)\s*(.*)/s )
                    {
                        $_SOCKET_BUFFER{$fh} = $2;
                        $xml_line = $1;
                    }
                    

                    #print length($_SOCKET_BUFFER{$fh}), "\n";
                    if ( $_SOCKET_BUFFER{$fh} =~ /^\s*(<BGP_MESSAGE.*?<\/BGP_MESSAGE>)\s*(.*)/s )
                    {
                      $_SOCKET_BUFFER{$fh} = $2;
                      $xml_line = $1;
                      $stat_data = XFB2STAT($xml_line);
                      push(@_XML_BUFFER,  $xml_line)  if ($xml_line);

                      if (defined $stat_data)
                      {
                        push(@_STAT_BUFFER, $stat_data);
                      }
                    }
                }

                my $time = time();
                #-----------------------------
                # Report status
                #-----------------------------
                if (($time - $_LAST_STATUS_TIME) >= $_STATUS_INTERVAL)
                {
                    $_LAST_STATUS_TIME = $time;

                    my $size = 0;
                    foreach my $fh (keys %_SOCKET_BUFFER)
                    {
                        $size += length($_SOCKET_BUFFER{$fh});
                    }

                    plog(sprintf("[main][loop:$i] _SOCKET_BUFFER: %5.5s(bytes) _STAT_BUFFER: %5.5s(entries)", 
                                                $size,
                                                scalar(@_STAT_BUFFER)));
                }
                #-----------------------------
                # Output buffered lines
                #-----------------------------
                if (($time - $_LAST_OUTPUT_TIME) >= $_OUTPUT_INTERVAL)
                {
                    $_LAST_OUTPUT_TIME = $time;

                    outputSTAT(@_STAT_BUFFER);
                    plog(sprintf("[main][loop:$i] Save %5.5s stat messages / Total %5.5s xml messages", 
                                                scalar(@_STAT_BUFFER), 
                                                scalar(@_XML_BUFFER))); # Log

                    # clear buffer
                    @_XML_BUFFER  = ();
                    @_STAT_BUFFER = ();
                }

            }
            # Reach here if timeout
            die "Timeout";

        };
        if ( $@ )
        { # There is an error
            chomp($@);

            # Log error
            plog(sprintf("%s", "[main][loop:$i][error] $@")); # Log the error
            
            # clear socket
            close($sock) if ($sock);
            undef $sock;
            undef $sel;

            # clear buffer
            foreach my $fh (keys %_SOCKET_BUFFER)
            {
                delete $_SOCKET_BUFFER{$fh};
            };
            %_SOCKET_BUFFER = ();

            @_XML_BUFFER = ();
        }
        plog(sprintf("%-75.75s", "[main][loop:$i][sleep]"));
        sleep(5);
    }
}



sub outputSTAT
{
  my (@stat_lines) = @_;
  
  for(my $i=0; $i<scalar(@stat_lines); $i++)
  {
     my $stat_data = $stat_lines[$i];

     my ($sec,$min,$hour,$mday,$mon,$year,$wday,$yday,$isdst) = localtime(time);
     $year += 1900;
     $mon  += 1;

     foreach my $KEY ("QUEUES", "CHAINS", "SESSIONS","MRTS")
     {
       if ( exists $stat_data->{$KEY} )
       {
         plog(sprintf("%-75.75s", "[outputSTAT] store message $KEY"));
         my $path        = sprintf("$_OUTPUT_DIR/$KEY/%4d.%2.2d/%2.2d", $year, $mon, $mday);
         mkpath($path);

         my $latest_file  = sprintf("$_OUTPUT_DIR/$KEY/LATEST.data");
         my $window_file  = sprintf("$_OUTPUT_DIR/$KEY/WINDOW.data");
         my $history_file = join("/", $path, sprintf("%4d%2.2d%2.2d.%2.2d.data", $year, $mon, $mday, $hour));

         #---------------------
         # Save latest file
         #---------------------
         my $lref = {};
         if(-e $latest_file){
           $lref = retrieve($latest_file);
         }
         my $dump_latest = 
                        { TIMESTAMP => $stat_data->{TIMESTAMP},
                          DATA      => $stat_data->{$KEY},
                         };
         $lref->{$stat_data->{'SRC'}} = 
                        { TIMESTAMP => $stat_data->{TIMESTAMP},
                          DATA      => $stat_data->{$KEY},
                          XML       => $stat_data->{RAW_XML},
                         };
            
         store($lref, $latest_file) or die "Can't store $latest_file!\n";

         #---------------------
         # Save window file
         #---------------------
         if (not -f $window_file)
        {
            my $dump_window = {};
            $dump_window->{$stat_data->{'SRC'}} = ();
            store($dump_window, $window_file) or die "Can't store $window_file!\n";
         }
         my $wref = retrieve($window_file);
         push @{ $wref->{$stat_data->{'SRC'} }}, $dump_latest;
         while( scalar( @{$wref->{$stat_data->{'SRC'}}} ) > $_WINDOW_SIZE )
         {
            shift @{$wref->{$stat_data->{'SRC'}}};
         }
         store($wref, $window_file) or die "Can't store $window_file!\n";

         #---------------------
         # Save history file
         #---------------------
         if (not -f $history_file)
         {
            my $dump_window = {};
            store($dump_window, $history_file) or die "Can't store $history_file!\n";
          }
          my $href = retrieve($history_file);
          push @{ $href->{$stat_data->{'SRC'}}}, $dump_latest;
          store($href, $history_file) or die "Can't store $history_file!\n";
       }
    }
  }
}



# vim: sw=4 ts=4 sts=4 expandtab
