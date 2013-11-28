package BGPmon::Client::ParseTable;

use 5.010001;
use strict;
use warnings;
use Carp;
use XML::Simple;

require Exporter;

our $AUTOLOAD;
our @ISA = qw(Exporter);
# This allows declaration	use BGPmon::Client ':all';
# If you do not need this, moving things directly into @EXPORT or @EXPORT_OK
# will save memory.
our %EXPORT_TAGS = ( 'all' => [ qw(new summarize_tables output_files) ] );
our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );
our @EXPORT = qw();
our $VERSION = '0.01';

# Items to export into callers namespace by default. Note: do not export
# names by default without a very good reason. Use EXPORT_OK instead.
# Do not simply export all your public functions/methods/constants.
my %fields = (
  output_file => undef,
  xml_buffer => '',
  tables => {},
);

sub new{
  my $class = shift;
  my $self = {
    _permitted => \%fields,
    %fields,
  };
  bless $self, $class;
  return $self;
} 


sub summarize_tables{

  my $self = shift;
  my $data_file = $self->{output_file};
  if(!defined($data_file)){
    return 0;
  }
  my $xml_string = shift;
  if(!defined($xml_string)){
    return 1;
  }
  $self->{xml_buffer} .= $xml_string;

  my $count = 0;
  while ( $self->{xml_buffer} =~ /^.*?(<BGP_MESSAGE.*?<\/BGP_MESSAGE>)\s*(.*)/s )
  {
    $count++;
    my $line = $1;
    $self->{xml_buffer} = $2;
    my $xs = XML::Simple->new();
    my $ref = $xs->XMLin($line,ForceArray => [ qw/UPDATE NLRI PREFIX/ ]);
    my $dst_address = $ref->{'PEERING'}->{'DST_ADDR'}->{'ADDRESS'};
    if($ref->{'type'} =~ /TABLE_START/){
      print STDERR "table start for $dst_address\n";
      if(defined($dst_address)){
        delete $self->{tables}->{$dst_address};
      }
    }elsif($ref->{'type'} =~ /TABLE_STOP/){
      print STDERR "table stop for $dst_address\n";
      open(OUT,">$data_file") or die "Unable to open data file $data_file\n";
      print OUT scalar(keys %{$self->{tables}}) . " total peers\n";
      foreach my $peer (sort keys %{$self->{tables}}){
         print OUT "$peer has " . scalar(keys(%{$self->{tables}->{$peer}})) . "\n\n";
      }
      close(OUT);
    }elsif($ref->{'type'} =~ /TABLE/){
      print STDERR "table for $dst_address\n";
      if($dst_address =~ /:/){
        use Data::Dumper;
        print $line . "\n";
      }
      foreach my $update(@{$ref->{'ASCII_MSG'}->{'UPDATE'}}){
        foreach my $nlri (@{$update->{'NLRI'}}){
          foreach my $prefix (@{$nlri->{'PREFIX'}}){
            $self->{tables}->{$dst_address}->{$prefix->{'ADDRESS'}} = 1;
          }
        }
      }
    }
  }
  return 1;
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
# Below is stub documentation for your module. You'd better edit it!

=head1 NAME

BGPmon::Client::ParseTable - Perl extension to do a cursury parsing of the RIB tables coming from BGPmon

=head1 SYNOPSIS

  use BGPmon::Client::ParseTable;
  my $parser = BGPmon::Client::ParseTable->new();
  while(getting a new xml_string in some way){
    $parser->summarize_table($xml_string);
  }

=head1 DESCRIPTION

This code is meant to work with the client code. 
summarize_table can be sent into the xml_handler function of Client.

=head2 EXPORT

new
summarize_table

=head1 SEE ALSO

=head1 AUTHOR

Catherine Olschanowsky, E<lt>cathie@cs.colostate.edu<gt>

=cut
