#!/usr/bin/perl -w 

#use lib qw(/usr/local/lib/perl/5.10.1);

use strict;
use warnings;

use File::Path;
use Template;
use Storable qw(store retrieve freeze thaw dclone);
use Chart::Gnuplot;
use Date::Format;

if (@ARGV != 2) {
     die "Invalid arguments!";
}

my ($html_dir, $data_dir) = @ARGV;
my $ver = "ver7-2-1";

eval {
	# some useful options (see below for full list)
	my $config = {
		#INCLUDE_PATH => '/search/path',  # or list ref
		INTERPOLATE  => 1,               # expand "$var" in plain text
		POST_CHOMP   => 1,               # cleanup whitespace 
		#PRE_PROCESS  => 'header',        # prefix each template
		EVAL_PERL    => 1,               # evaluate Perl code blocks
	};

	# define template variables for replacement
	my $vars = {};

	# QUEUE
	{
		my $queue_latest_file = "$data_dir/QUEUES/LATEST.data";
		my $ref = retrieve($queue_latest_file) or die "cannot open $queue_latest_file";
                $vars->{QUEUES} = $ref;

		my $queue_window_file = "$data_dir/QUEUES/WINDOW.data";
		my $wref = retrieve($queue_window_file) or die "cannot open $queue_window_file";

                foreach my $src (keys %$wref){
		  # Plot queue usage
		  my $chart_queue_usage = generateChartQueueUsage($html_dir,$wref->{$src},$src); 
		  $vars->{PIC}->{QUEUE_USAGE}->{$src} = $chart_queue_usage;

		  # Plot queue damping
		  my $chart_queue_damping = generateChartQueueDampingAll($html_dir, $wref->{$src},$src); 
		  $vars->{PIC}->{QUEUE_DAMPING}->{$src} = $chart_queue_damping;
                }
	}

	# CHAIN
	{
		my $chain_latest_file = "$data_dir/CHAINS/LATEST.data";
		my $ref = retrieve($chain_latest_file) or die "cannot open $chain_latest_file";
                foreach my $src (keys %$ref){
                  foreach (keys %{$ref->{$src}}){
                    if($_ =~ /DATA/ && defined($vars->{CHAINS}->{$_})){
                      push @{$vars->{CHAINS}->{$_}},@{$ref->{$src}->{$_}};
                    }else{
		      $vars->{CHAINS}->{$_} = $ref->{$src}->{$_};
                    }
                  }
                }
	}

	# SESSION
	{
		my $session_latest_file = "$data_dir/SESSIONS/LATEST.data";
		my $ref = retrieve($session_latest_file) or die "cannot open $session_latest_file";
                foreach my $src (keys %$ref){
                  foreach (keys %{$ref->{$src}}){
                    if($_ =~ /DATA/ && defined($vars->{SESSIONS}->{$_})){
                      push @{$vars->{SESSIONS}->{$_}},@{$ref->{$src}->{$_}};
                    }else{
		      $vars->{SESSIONS}->{$_} = $ref->{$src}->{$_};
                    }
                  }
                }

                foreach my $src (keys %$ref){
		  my $session_window_file = "$data_dir/SESSIONS/WINDOW.data";
		  my $wref = retrieve($session_window_file) or die "cannot open $session_window_file";
                  my $chart_session = generateChartSessionAll($html_dir, $ref, $wref,$src); 
		  $vars->{PIC}->{SESSION_STAT} = $chart_session;
                }
	 }
	 # MRT
	 {
		my $mrt_latest_file = "$data_dir/MRTS/LATEST.data";
		my $ref = retrieve($mrt_latest_file) or die "cannot open $mrt_latest_file";
                foreach my $src (keys %$ref){
                  foreach (keys %{$ref->{$src}}){
                    if($_ =~ /DATA/ && defined($vars->{MRTS}->{$_})){
                      push @{$vars->{MRTS}->{$_}},@{$ref->{$src}->{$_}};
                    }else{
		      $vars->{MRTS}->{$_} = $ref->{$src}->{$_};
                    }
                  }
                }

                #foreach my $src (keys %$ref){
		  #my $mrt_window_file = "$data_dir/MRTS/WINDOW.data";
		  #my $wref = retrieve($mrt_window_file) or die "cannot open $mrt_window_file";
                  #my $chart_mrt = generateChartSessionAll($html_dir, $ref, $wref,$src); 
		  #$vars->{PIC}->{MRT_STAT} = $chart_mrt;
	        #}
         }




        $vars->{RAND} = rand(10000000000);

	# create Template object
	my $template = Template->new($config);

	# specify input filename, or file handle, text reference, etc.
	my $input = 'stat.tmpl.html';

	# generate timestamp for webpage
        my @t = localtime(time);
	my $date = sprintf("%02d-%02d-%4d", $t[4]+1, $t[3], $t[5]+1900);
	my $time = sprintf("%02d:%02d", $t[2], $t[1]);
	$vars->{DATESTAMP} = $date;
	$vars->{TIMESTAMP} = $time;

	# process input template, substituting variables
	my $output_file = join("/", $html_dir, "stat_$ver.html");
	$template->process($input, $vars, $output_file) || die $template->error();

};

if ($@)
{
    printf("%s", $@);
}


sub generateChartQueueUsage
{
    my ($html_dir, $wref, $src) = @_;
    my $pic_dir  = join("/", $html_dir, "pic");
    my $pic_url  = join("_", $ver, "queue_usage$src.png");
    my $pic_file = join("/", $pic_dir,  $pic_url);
    mkpath($pic_dir);
    
    my $time_end   = time;
    my $time_begin = time - 24*60*60;

    my $count = 0;
    my @x  = ();
    my @y_peer  = ();
    my @y_label = ();
    my @y_xml   = ();
    foreach my $entry ( @ $wref )
    {
        next if ($entry->{TIMESTAMP} < $time_begin);

        my $ts = time2str("%Y.%m.%d.%H.%M", $entry->{TIMESTAMP});
        push @x, $ts;

        foreach my $queue ( @{ $entry->{DATA} } )
        {
            my $usage = $queue->{item}->{current_usage};
            push(@y_peer, $usage ) if ($queue->{name} eq 'PeerQueue');
            push(@y_label,$usage ) if ($queue->{name} eq 'LabelQueue');
            push(@y_xml,  $usage ) if ($queue->{name} eq 'XMLUQueue');
        } 
        $count++;
    }

    # Create chart object and specify the properties of the chart
    $ENV{'GNUTERM'} = 'dumb';
    my $chart = Chart::Gnuplot->new(
        output   => $pic_file,
        title    => "BGPMon Queue Usage",
        xlabel   => "Date/Time",
        ylabel   => "Utilization(%)",
        #xdata   => "time",
        grid     => "on",
        timeaxis => "x",
        #xrange  => ["2009.02.04.23.40", "2009.02.05.23.40"],
        #xrange  => ["2008.5.2.16.20", "2008.5.2.16.30"],
        timefmt => '"%Y.%m.%d.%H.%M"',
        #xrange   => ["2008.5.2.16.20","2009.5.3.16.50"],
        #xrange   => ["2008.5", "2009.6"],
        yrange   => [0, 100],
        #timestamp => 'on'
        #format x "%m.%d.%H.%M"
        #xtics     => { labelfmt => '%b %d %H', },
        xtics     => { labelfmt  => '%h %d\n%H:%M', 
                       font      => "arial",
                       fontsize  => 11,
                     },
        #timestamp => { fmt    => '%Y.%h.%d.%H.%M' }, 
    );


    # Create dataset object and specify the properties of the dataset
    my $dataSet1 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_peer,
        title => "PeerQueue",
        style => "linespoints",
        timefmt   => "%Y.%m.%d.%H.%M",
        pointtype => 4,
    );

    my $dataSet2 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_label,
        title => "LabelQueue",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet3 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_xml,
        title => "XMLUQueue",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    # Plot the data set on the chart
    $chart->plot2d($dataSet1, $dataSet2, $dataSet3);

    return $pic_url;
}

sub generateChartQueueDampingAll
{
    my ($html_dir, $wref,$src) = @_;

    my $dampling_pic = {};
    $dampling_pic->{PeerQueue}  = generateChartQueueDamping($html_dir,  "PeerQueue",  $wref,$src);
    $dampling_pic->{LabelQueue} = generateChartQueueDamping($html_dir,  "LabelQueue", $wref,$src);
    $dampling_pic->{XMLUQueue}  = generateChartQueueDamping($html_dir,  "XMLUQueue",  $wref,$src);
    $dampling_pic->{XMLRQueue}  = generateChartQueueDamping($html_dir,  "XMLRQueue",  $wref,$src);

    return $dampling_pic;
}

sub generateChartQueueDamping
{
    my ($html_dir, $queue_name, $wref,$src) = @_;
    my $pic_dir  = join("/", $html_dir, "pic");
    my $pic_url  = join("_", $ver, "queue_damping_$queue_name"."_"."$src.png");
    my $pic_file = join("/", $pic_dir,  $pic_url);
    mkpath($pic_dir);
    
    my $time_end   = time;
    my $time_begin = time - 24*60*60;

    my $count = 0;
    my %time_data  = ();
    foreach my $entry ( @ $wref )
    {
        next if ($entry->{TIMESTAMP} < $time_begin);

        my $ts = time2str("%Y.%m.%d.%H", $entry->{TIMESTAMP});
        foreach my $queue ( @{ $entry->{DATA} } )
        {
            if ($queue->{name} eq $queue_name)
            {
                $time_data{$ts} = {} if (not exists $time_data{$ts});
                $time_data{$ts}{'PacingCount'} += $queue->{pacing}->{count}->{current};
                $time_data{$ts}{'PacingCountNum'}++;
                $time_data{$ts}{'WriteLimit'}  += $queue->{pacing}->{write_limit}->{current};
                $time_data{$ts}{'WriteLimitNum'}++;
            }
        } 
        $count++;
    }

    my @x  = ();
    my @y_pc = ();
    my @y_wl = ();
    foreach my $ts (sort keys %time_data)
    {
        push @x, $ts;
        #push(@y_pc, $time_data{$ts}{'PacingCount'} / $time_data{$ts}{'PacingCountNum'}); 
        #push(@y_wl, $time_data{$ts}{'WriteLimit'}  / $time_data{$ts}{'WriteLimitNum'}); 
        push(@y_pc, $time_data{$ts}{'PacingCount'}); 
        push(@y_wl, $time_data{$ts}{'WriteLimit'}); 
    }

#    foreach my $entry ( @ $wref )
#    {
#        #print $time_begin, " ", $entry->{TIMESTAMP}, "\n";
#        next if ($entry->{TIMESTAMP} < $time_begin);
#
#        use Data::Dumper;
#        #print Dumper($entry);
#        my $ts = time2str("%Y.%m.%d.%H.%M", $entry->{TIMESTAMP});
#        push @x, $ts;
#
#        foreach my $queue ( @{ $entry->{DATA} } )
#        {
#            my $pacing_count = $queue->{pacing}->{count}->{current};
#            my $write_limit  = $queue->{pacing}->{write_limit}->{current};
#            push(@y_pc, $pacing_count) if ($queue->{name} eq $queue_name);
#            push(@y_wl, $write_limit ) if ($queue->{name} eq $queue_name);
#        } 
#        $count++;
#    }
#    print $count, "/", scalar(@$wref), "\n";

    #print Dumper(\@x);
    #print Dumper(\@y_pc);
    #print Dumper(\@y_wl);
    #return;

    # Create chart object and specify the properties of the chart
    my $chart = Chart::Gnuplot->new(
        output   => $pic_file,
        title    => "BGPMon Queue Damping",
        xlabel   => "Date/Time",
        ylabel   => "Number",
        #xdata   => "time",
        grid     => "on",
        timeaxis => "x",
        #timefmt => "%Y.%m.%d.%H.%M",
        #xrange  => ["2009.02.04.23.40", "2009.02.05.23.40"],
        #xrange  => ["2008.5.2.16.20", "2008.5.2.16.30"],
        yrange  => '[0.1:]',
        #timestamp => 'on'
        #format x "%m.%d.%H.%M"
        #xtics     => { labelfmt => '%b %d %H', },
        xtics     => { labelfmt => '%h %d\n%H:%M', 
                       font      => "arial",
                       fontsize  => 11,
                     },
        #timestamp => { fmt    => '%Y.%h.%d.%H.%M' }, 
        log      => "y",
    );


    # Create dataset object and specify the properties of the dataset
    my $dataSet1 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_pc,
        title => "Damping Count",
        style => "linespoints",
        timefmt   => "%Y.%m.%d.%H.%M",
        pointtype => 4,
    );

    my $dataSet2 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_wl,
        title => "Damping Limit(per second)",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    # Plot the data set on the chart
    $chart->plot2d($dataSet1, $dataSet2);
    #$chart->plot2d($dataSet1, $dataSet2, $dataSet3);

    return $pic_url;
}

sub generateChartSessionAll
{
    my ($html_dir, $ref, $wref, $src) = @_;

    my $session_pic = {};

    use Data::Dumper;
    #print Dumper($ref);

    foreach my $session ( @{ $ref->{$src}->{DATA} } )
    {
        my $key = sprintf("%s_%s", $session->{addr}, $session->{port});
#        print $key, "\n";
        $session_pic->{$key} = generateChartSession($html_dir, $session->{addr}, $session->{port}, $wref->{$src});
    }
    return $session_pic;
}

sub generateChartSession
{
    my ($html_dir, $addr, $port, $wref) = @_;
    my $pic_dir  = join("/", $html_dir, "pic");
    my $pic_url  = join("_", $ver, sprintf("session_%s_%s.png", $addr, $port));
    my $pic_file = join("/", $pic_dir,  $pic_url);
    mkpath($pic_dir);
    
    my $time_end   = time;
    my $time_begin = time - 24*60*60;

    my $count = 0;
    my %time_data  = ();
    foreach my $entry ( @ $wref )
    {
        next if ($entry->{TIMESTAMP} < $time_begin);

        my $ts = time2str("%Y.%m.%d.%H", $entry->{TIMESTAMP});
        foreach my $session ( @{ $entry->{DATA} } )
        {
            #use Data::Dumper;
            #print Dumper($session);

            if ($session->{addr} eq $addr and
                $session->{port} eq $port)
            {
                $time_data{$ts} = {} if (not exists $time_data{$ts});

                # Anno
                $time_data{$ts}{'Anno'} += $session->{anno}->{current};
                $time_data{$ts}{'AnnoNum'} ++;
                # Dup Anno
                $time_data{$ts}{'DupAnno'} += $session->{dup_anno}->{current};
                $time_data{$ts}{'DupAnnoNum'} ++;
                # Same Path
                $time_data{$ts}{'SPath'} += $session->{same_path}->{current};
                $time_data{$ts}{'SPathNum'} ++;
                # Diff Path
                $time_data{$ts}{'DPath'} += $session->{diff_path}->{current};
                $time_data{$ts}{'DPathNum'} ++;
                # Withd
                $time_data{$ts}{'Withd'} += $session->{withdrawal}->{current};
                $time_data{$ts}{'WithdNum'} ++;
                # Dup Withd
                $session->{dup_withdrawal} = $session->{dup_withdrawl} if (exists $session->{dup_withdrawl});
                $time_data{$ts}{'DupWithd'} += $session->{dup_withdrawal}->{current};
                $time_data{$ts}{'DupWithdNum'} ++;
            }
        } 
        $count++;
    }

    my @x  = ();
    my @y_anno      = ();
    my @y_dup_anno  = ();
    my @y_same_path = ();
    my @y_diff_path = ();
    my @y_withd     = ();
    my @y_dup_withd = ();
    foreach my $ts (sort keys %time_data)
    {
        push @x, $ts;
        #push(@y_anno,      $time_data{$ts}{'Anno'}     / $time_data{$ts}{'AnnoNum'}); 
        #push(@y_dup_anno,  $time_data{$ts}{'DupAnno'}  / $time_data{$ts}{'DupAnnoNum'}); 
        #push(@y_same_path, $time_data{$ts}{'SPath'}    / $time_data{$ts}{'SPathNum'}); 
        #push(@y_diff_path, $time_data{$ts}{'DPath'}    / $time_data{$ts}{'DPathNum'}); 
        #push(@y_withd,     $time_data{$ts}{'Withd'}    / $time_data{$ts}{'WithdNum'}); 
        #push(@y_dup_withd, $time_data{$ts}{'DupWithd'} / $time_data{$ts}{'DupWithdNum'}); 
        push(@y_anno,      $time_data{$ts}{'Anno'});
        push(@y_dup_anno,  $time_data{$ts}{'DupAnno'});
        push(@y_same_path, $time_data{$ts}{'SPath'});
        push(@y_diff_path, $time_data{$ts}{'DPath'});
        push(@y_withd,     $time_data{$ts}{'Withd'});
        push(@y_dup_withd, $time_data{$ts}{'DupWithd'});
    }
    @y_anno      = map { ($_ ne 0) ? $_ : 0.1 } @y_anno;
    @y_dup_anno  = map { ($_ ne 0) ? $_ : 0.1 } @y_dup_anno;
    @y_same_path = map { ($_ ne 0) ? $_ : 0.1 } @y_same_path;
    @y_diff_path = map { ($_ ne 0) ? $_ : 0.1 } @y_diff_path;
    @y_withd     = map { ($_ ne 0) ? $_ : 0.1 } @y_withd;
    @y_dup_withd = map { ($_ ne 0) ? $_ : 0.1 } @y_dup_withd;
	
#    foreach my $entry ( @ $wref )
#    {
#        #print $time_begin, " ", $entry->{TIMESTAMP}, "\n";
#        next if ($entry->{TIMESTAMP} < $time_begin);
#
#        use Data::Dumper;
#        my $ts = time2str("%Y.%m.%d.%H.%M", $entry->{TIMESTAMP});
#        push @x, $ts;
#
#        foreach my $session ( @{ $entry->{DATA} } )
#        {
#            if ($session->{addr} eq $addr and
#                $session->{port} eq $port)
#            {
#                #print Dumper($session);
#                push(@y_anno,      $session->{anno}->{current});
#                push(@y_dup_anno,  $session->{dup_anno}->{current});
#                push(@y_same_path, $session->{same_path}->{current});
#                push(@y_diff_path, $session->{diff_path}->{current});
#                push(@y_withd,     $session->{withdrawal}->{current});
#                # temp 
#                if (exists $session->{dup_withdrawl})  { push(@y_dup_withd, $session->{dup_withdrawl}->{current});  }
#                if (exists $session->{dup_withdrawal}) { push(@y_dup_withd, $session->{dup_withdrawal}->{current}); }
#            }
#        } 
#        $count++;
#    }
    #print $count, "/", scalar(@$wref), "\n";

    #print Dumper(\@x);
    #print Dumper(\@y_anno);
    #print Dumper(\@y_dup_anno);
    #print Dumper(\@y_same_path);
    #print Dumper(\@y_diff_path);
    #print Dumper(\@y_withd);
    #print Dumper(\@y_dup_withd);
    #return;

    # Create chart object and specify the properties of the chart
    my $chart = Chart::Gnuplot->new(
        output   => $pic_file,
        title    => "BGPMon Peer Status ($addr : $port)",
        xlabel   => "Date/Time",
        ylabel   => "Number",
        #xdata   => "time",
        grid     => "on",
        timeaxis => "x",
        yrange  => '[0.1:]',
        #timefmt => "%Y.%m.%d.%H.%M",
        #xrange  => ["2009.02.04.23.40", "2009.02.05.23.40"],
        #xrange  => ["2008.5.2.16.20", "2008.5.2.16.30"],
        #yrange  => '[0:100]',
        #timestamp => 'on'
        #format x "%m.%d.%H.%M"
        #xtics     => { labelfmt => '%b %d %H', },
        xtics     => { labelfmt => '%h %d\n%H:%M', 
                       font      => "arial",
                       fontsize  => 11,
                     },
        #timestamp => { fmt    => '%Y.%h.%d.%H.%M' }, 
        log      => "y",
    
    );


    # Create dataset object and specify the properties of the dataset
    my $dataSet1 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_anno,
        title => "Announcement",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet2 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_dup_anno,
        title => "Dup Announcement",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet3 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_same_path,
        title => "Same Path",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet4 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_diff_path,
        title => "Diff Path",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet5 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_withd,
        title => "Withdrawal",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    my $dataSet6 = Chart::Gnuplot::DataSet->new(
        xdata => \@x,
        ydata => \@y_dup_withd,
        title => "Dup Withdrawal",
        style => "linespoints",
        timefmt => "%Y.%m.%d.%H.%M",
    );

    $chart->plot2d($dataSet1, $dataSet2, $dataSet3, $dataSet4, $dataSet5, $dataSet6);

    return $pic_url;
}

# vim: sw=4 ts=4 sts=4 expandtab
