#! /usr/bin/perl

use XML::Smart;

my $xsd_file = $ARGV[0];

### Create the object and load the file:
my $XSD = XML::Smart->new($xsd_file, 'XML::Smart::Parser');

set_key_order($XSD->nodes);

print $XSD->data("nometagen"=>1);

sub set_key_order
{
	my (@node) = @_;

	foreach my $node (@nodes)
	{
		#print $node->order(), "\n";
		#$node->set_order("type", "name", "minOccurs", "maxOccurs");
		$node->set_order("ref", "name", "type", "minOccurs", "maxOccurs");
		#print $node->order(), "\n";
		#print $node->data_pointer;

		foreach my $child ($node->nodes)
		{
			#print $i++, "\n";
			&set_key_order($child);
		}
	}
}
