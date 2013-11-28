package BGPmon::Client;

use 5.010001;
use strict;
use warnings;
use Carp;
use IO::Select;
use IO::Socket;

require Exporter;

our $AUTOLOAD;
our @ISA = qw(Exporter);
# This allows declaration	use BGPmon::Client ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(new connect address port xml_handler xml_handler_params run clear_errors) ] );
our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );
our @EXPORT = qw();
our $VERSION = '0.01';

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
my %fields = (
  address => undef,
  port => undef,
  xml_handler => undef,
  xml_handler_params => undef,
  sock => undef,
  sel => undef,
  error => undef,
  error_count => 0,
  max_read_len => 1024 );
  
sub new {
  my $class = shift;
  my $self = {
   _permitted => \%fields,
   %fields,
  };
  bless ($self, $class);
  return $self;
}

#-------------------------------------------------------
# Connect to server
#-------------------------------------------------------
sub connect{
  my $self = shift;
  $self->{sock} = new IO::Socket::INET (
                      PeerAddr  => $self->{address},
                      PeerPort  => $self->{port},
                      Proto => 'tcp');
  if(!$self->{sock}){
    $self->add_error("Unable to create new socket\n");
    return 0;
  }
  $self->{sel}  = new IO::Select($self->{sock});
  if(!defined($self->{sel})){
    $self->add_error("Unable to create new select\n");
    return 0;
  }
  return 1;
}

sub run{
  my $self = shift;
  my $timeout = undef;
  if(@_){
    $timeout = shift;
  }
  my @ready = ();


  if(!defined($self->{sel})){
    $self->add_error("Select is undef in running --> was connect called successfully?\n");
    return 0;
  }
  my $start_time = time;
  while(@ready = $self->{sel}->can_read($timeout))
  {
    #-----------------------------
    # Read from each ready socket: this is in a loop in case we ever expand connect to be used with more than one socket.
    # as of now there will be 0 or one of these only. If multiple sockets are added we will need to add multiple
    # xml_handlers, one corresponding to each handler.
    #-----------------------------
    foreach my $fh (@ready)
    {
        my $text      = "";
        my $xml_line  = "";
        my $stat_data = "";

        # There are three possible return values of sysread
        # http://perldoc.perl.org/functions/sysread.html
        my $readlen = sysread($fh, $text, $self->{max_read_len});
    
        if ($readlen > 0)
        { # (1) normal 
            if(defined($self->{xml_handler_params})){
              if(!&{$self->{xml_handler}}($self->{xml_handler_params},$text)){
                $self->add_error("xml handler returned error code\n");
                return 0;
              }
            }else{
              if(!&{$self->{xml_handler}}($text)){
                $self->add_error("xml handler returned error code\n");
                return 0;
              }
            }
        }
        elsif ($readlen == 0)
        { # (2) end of socket
            # Raise exception
            $self->add_error("End of Socket\n");
            return 0;
        }
        else
        { # (3) error
    
            # Raise exception
            $self->add_error("readlen returned less than 1\n");
            return 0;
        }
    }
    my $current_time = time;
    if(defined($timeout)){
      if($current_time - $start_time > $timeout){
        last;
      }
    }
  }
  return 1;
}

sub add_error{
  my $self = shift;
  my $error = shift;
  $self->{error_count}++;
  if(defined($self->{error})){
    $self->{error} .= $error;
  }else{
    $self->{error} = $error;
  }
  return 1;
}

sub clear_errors{
  my $self = shift;
  $self->{error} = undef;
  $self->{error_count} = 0;
}

#-------------------------------------------------------
# Accessor Functions
#-------------------------------------------------------

sub AUTOLOAD {
 my $self = shift;
 my $type = ref($self) or croak "$self is not an object";

 my $name = $AUTOLOAD;
 $name =~ s/.*://; # strip fully-qualified portion

 unless (exists $self->{_permitted}->{$name} ) {
   croak "Can't access `$name' field in class $type";
 }

 if (@_) {
 return $self->{$name} = shift;
 } else {
 return $self->{$name};
 }
}

sub DESTROY { }
# Preloaded methods go here.


1;
__END__

=head1 NAME

BGPmon::Client - Perl extension for creating a BGPmon client

=head1 SYNOPSIS

  use BGPmon::Client;
  my $client = BGPmon::Client->new();
  $client->address('127.0.0.1');
  $client->port(50002);
  $client->xml_handler(\&process_func); # you have to create the process_fun
  if(!$client->connect()){
    print $client->error();
    return 0;
  }
  if(!$client->run(10)){ # runs the client for 10 seconds
    print $client->error();
    return 0;
  }


=head1 DESCRIPTION

This code is meant to help write BGPmon clients. It is the listening code for a single
socket connection with a single filehandle. 
It is meant to be accompanied by one of the parsing helper classes also defined in this 
package. The parsing helper class should define the function pointer sent into the
xml_handler.


=head2 EXPORT

new
address
port
connect
error
error_count
xml_handler
xml_handler_params


=head1 SEE ALSO


=head1 AUTHOR

Catherine Olschanowsky, <lt>cathie@cs.colostate.edu<gt>

=cut
