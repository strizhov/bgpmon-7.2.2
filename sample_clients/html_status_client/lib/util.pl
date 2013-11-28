use IO::Socket;
use XML::LibXML;
use Storable;
use Time::Format qw(%time %strftime %manip);

sub XFB2STAT
{
  my ($xml_string) = @_;
  my $stat_data    = {};

  # Parse XML message
  my $parser = XML::LibXML->new();
  my $tree = $parser->parse_string($xml_string);

  # Setup pointers to elements
  my $bgp_message      = $tree->getDocumentElement;
  # we are only interested in messages that have a status node
  my $status_msg_node = $bgp_message->getElementsByTagName('STATUS_MSG');
  return undef if (not $status_msg_node);
  plog("$xml_string\n\n");
  my $seq_node = $bgp_message->getElementsByTagName('BGPMON_SEQ');

  # Store raw xml string
  $stat_data->{'RAW_XML'} = $bgp_message->toString(1);
  my $time_nodes = $bgp_message->getElementsByTagName('TIME');
  if($time_nodes->size() != 1){
    plog("[XFB2STAT]: TIME node formatting error " . $xml_string . "\n");
    return undef;
  }
  $stat_data->{'TIMESTAMP'} = $time_nodes->get_node(0)->getAttribute("timestamp");;
  $stat_data->{'SRC'} = $seq_node->get_node(0)->getAttribute("id");

  # Parse child elements
  my $bgpmon_node         = $status_msg_node->get_node(0)->getElementsByTagName('BGPMON');
  my $queue_status_node   = $status_msg_node->get_node(0)->getElementsByTagName('QUEUE_STATUS');
  my $chain_status_node   = $status_msg_node->get_node(0)->getElementsByTagName('CHAIN_STATUS');
  my $session_status_node = $status_msg_node->get_node(0)->getElementsByTagName('SESSION_STATUS');
  my $mrt_status_node     = $status_msg_node->get_node(0)->getElementsByTagName('MRT_STATUS');

  # QUEUE
  if ($bgpmon_node)
  {
    $stat_data->{'BGPMON'} = parseBgpmon($bgpmon_node);
  }

  # QUEUE
  if ($queue_status_node)
  {
    my $count  = $queue_status_node->get_node(0)->getAttribute("count");
    my @queues = $queue_status_node->get_node(0)->getChildrenByTagName("QUEUE");
    $stat_data->{'QUEUES'} = [ map { parseQueue($_) } @queues ];
  }

  # CHAIN
  if ($chain_status_node)
  {
    my $count  = $chain_status_node->get_node(0)->getAttribute( "count");
    my @chains = $chain_status_node->get_node(0)->getChildrenByTagName("CHAIN");
    $stat_data->{'CHAINS'} = [ map { parseChainStat($_) } @chains ];
  }
  # SESSION
  if ($session_status_node)
  {
    my $count    = $session_status_node->get_node(0)->getAttribute( "count");
    my @sessions = $session_status_node->get_node(0)->getChildrenByTagName("SESSION");
    foreach my $session ( @sessions )
    {
      my $session_data = { (%{parsePeeringStat($session)}, %{parseSessionStat($session)}) }; 


      if (exists $session_data->{state} and defined $session_data->{state})
      {
          push @{ $stat_data->{'SESSIONS'} }, $session_data;
      }
    }
  }

  #------------------------------
  # MRT
  #------------------------------
  if ($mrt_status_node)
  {
    my @sessions = $mrt_status_node->get_node(0)->getChildrenByTagName("SESSION");
    foreach my $session ( @sessions )
    {
      my $session_data = { (%{parsePeeringStat($session)}, %{parseSessionStat($session)}) }; 


      if (exists $session_data->{state} and defined $session_data->{state})
      {
        push @{ $stat_data->{'MRTS'} }, $session_data;
      }
    }
  }

  return undef if (not (exists $stat_data->{'SESSIONS'} or
                          exists $stat_data->{'QUEUES'}   or
                          exists $stat_data->{'MRTS'}   or
                          exists $stat_data->{'CHAINS'} )
                    );
  undef $parser;
  return $stat_data;
}

sub parseStat
{
    my ($stat_node) = @_;
    my %stat  = (
                    current => $stat_node->textContent,
                    max     => $stat_node->getAttribute("max"),
                    limit   => $stat_node->getAttribute("limit"),
                    current_usage   => 0,
                    max_usage       => 0,
                );
    if ($stat{limit} > 0)
    {
        $stat{current_usage} = sprintf("%2.2f", 100 * $stat{current} / $stat{limit}); 
        $stat{max_usage}     = sprintf("%2.2f", 100 * $stat{max}     / $stat{limit}); 
    }
    return \%stat;
}

sub parseTime
{
    my ($node) = @_;
    my %stat  = (
                    current => $node->textContent,
                    last_down    => $node->getAttribute("last_down"),
                    last_action  => $node->getAttribute("last_action"),
                );

    if ( $stat{current} )
    {
        $stat{current_fmt} = sprintf("%d days %d hours %d min %d sec", int($stat{current}/3600/24), 
                                                                       int(($stat{current}%(3600*24))/3600), 
                                                                       int(($stat{current}%3600)/60), 
                                                                       int($stat{current}%60));
    }
    $stat{last_down_fmt}   = $time{'Weekday Month d HH:mm:ss, yyyy', $stat{last_down}   } if ( $stat{last_down}   > 0 );
    $stat{last_action_fmt} = $time{'Weekday Month d HH:mm:ss, yyyy', $stat{last_action} } if ( $stat{last_action} > 0 );
    return \%stat;
}

sub parsePacing
{
    my ($node) = @_;
    my %pacing = (
                    #count        => ( $pacing_node->getElementsByTagName("COUNT")       ) ?  parseStat($pacing_node->getElementsByTagName("COUNT"))       : undef,
                    #write_limit  => ( $pacing_node->getElementsByTagName("WRITE_LIMIT") ) ?  parseStat($pacing_node->getElementsByTagName("WRITE_LIMIT")) : undef,
                    count        => parseChildStat($node, "COUNT"),
                    write_limit  => parseChildStat($node, "WRITE_LIMIT"),
                 );
    return \%pacing;
}

sub parseChildStat
{
    my ($node, $tag) = @_;
    return  ($node->getElementsByTagName($tag)) ?  parseStat($node->getElementsByTagName($tag))   : undef;
}

sub parseChildTime
{
    my ($node, $tag) = @_;
    return  ($node->getElementsByTagName($tag)) ?  parseTime($node->getElementsByTagName($tag))   : undef;
}

sub parseChildText
{
    my ($node, $tag) = @_;
    return ( $node->getElementsByTagName($tag)) ?  ($node->getElementsByTagName($tag))[0]->textContent : undef
}

sub parseChildPacing
{
    my ($node, $tag) = @_;
    return  ($node->getElementsByTagName($tag)) ?  parsePacing($node->getElementsByTagName($tag))   : undef;
}

sub parseBgpmon
{
    my ($node) = @_;
    my %bgpmon = (
                    addr   => parseChildText($node, "ADDR"),
                    port   => parseChildText($node, "PORT"),
                    as     => parseChildText($node, "AS"),
                    
                );
    return \%bgpmon;
}

sub parseQueue
{
    my ($node) = @_;
    my %queue = (
                    name   => parseChildText($node, "NAME"),
                    item   => parseChildStat($node, "ITEM"),
                    writer => parseChildStat($node, "WRITER"),
                    reader => parseChildStat($node, "READER"),
                    pacing => parseChildPacing($node, "PACING"),
                    
                );
    return \%queue;
}

sub parseChainStat
{
  my ($node) = @_;
  my %cstat = (
    addr  => parseChildText($node, "UPDATE_ADDR"),
    port  => parseChildText($node, "UPDATE_PORT"),
    state => parseChildText($node, "UPDATE_STATE"),
    uptime=> parseChildTime($node, "UPDATE_UPTIME"),
    recv  => parseChildStat($node, "UPDATE_RECV_MESSAGE"),
    reset => parseChildStat($node, "UPDATE_RESET"),
  );
  return \%cstat;
}

sub parsePeeringStat
{
    my ($node) = @_;
    my %pstat = (
                    addr    => parseChildText($node, "ADDR"),
                    port    => parseChildText($node, "PORT"),
                    as      => parseChildText($node, "AS"),
                    state   => parseChildText($node, "STATE"),
                    uptime  => parseChildTime($node, "UPTIME"),
                    recv    => parseChildStat($node, "RECV_MESSAGE"),
                    reset   => parseChildStat($node, "RESET"),
                );
    return \%pstat;
}

sub parseSessionStat
{
    my ($node) = @_;
    my %sstat = (
                    prefix    => parseChildStat($node, "PREFIX"),
                    attribute => parseChildStat($node, "ATTRIBUTE"),
                    memory    => parseChildStat($node, "MEMORY_USAGE"),

                    anno      => parseChildStat($node, "ANNOUNCEMENT"),
                    dup_anno  => parseChildStat($node, "DUP_ANNOUNCEMENT"),
                    same_path => parseChildStat($node, "SAME_PATH"),
                    diff_path => parseChildStat($node, "DIFF_PATH"),
                    withdrawal     => parseChildStat($node, "WITHDRAWAL"),
                    dup_withdrawal => parseChildStat($node, "DUP_WITHDRAWAL"),
                );
    $sstat{memory}{current_kb} = $sstat{memory}{current} / 1000;
    return \%sstat;
}

1;

# vim: sw=4 ts=4 sts=4 expandtab
