#! /usr/bin/perl

use XML::Smart;
my $txt_file = $ARGV[0];
my $xsd_file = $ARGV[1];

### Create the object and load the file:
my $XML = XML::Smart->new($txt_file, 'XML::Smart::Parser');
my $XSD = XML::Smart->new($xsd_file, 'XML::Smart::Parser');

### Parse middle
my $middle = $XML->{'rfc'}{'middle'};
#visit_middle($middle);
print visit_middle($middle);

exit;

sub visit_middle {
    my ($node) = @_;
    my @string = ();
    printf STDERR ("[%s]\n", $node->key());

    foreach my $child ($node->nodes())
    {
        my $key = $child->key();
        if ($key eq 'section')
        {
            push @string, visit_section($child, $level);
        }
        else
        {
            print STDERR "ERROR: $node->key() $key is not handled\n";
        }
    }
    return join("\n", @string);
}

sub visit_section {
    my ($node, $level) = @_;
    my @string = ();
    #$node = $node->cut_root;
    #print $node->key(), "\n";
    printf STDERR ("[%s]\n", $node->key());

    $level = (not defined $level) ? 1 : $level+1;
    push @string, "=" x $level . $node->{'title'} . "=" x $level, "\n";

    foreach my $child ($node->nodes())
    {
        my $key = $child->key();
        #print $key, "\n";
        if ($key eq 't')
        {
            push @string, visit_t($child);
        }
        elsif ($key eq 'section')
        {
            push @string, visit_section($child, $level);
        }
        elsif ($key eq 'figure') 
        {
            push @string, visit_figure($child);
        }
        elsif ($key eq 'texttable') 
        {
            push @string, visit_texttable($child);
        }
        else
        {
            #print STDERR $key, "\n";
        }
        undef $child;
    }
    
    #print join("\n", @string);
    return join("\n", @string);
}

sub visit_t {
    my ($node) = @_;
    my @string = ();
    printf STDERR ("[%s]\n", $node->key());

    my $content = $node->content;
    $content =~ s/[\n]//mg;
    $content =~ s/\s+/ /mg;
    my $hangText = $node->{'hangText'};
    $content = $hangText . "\n:" . $content if ($hangText);

    push @string, $content;
    if ($node->nodes())
    {
        foreach my $child ($node->nodes())
        {
            my $key = $child->key();
            #print $key, "\n";

            if ($key eq 'list')
            {
                push @string, visit_list($child);
            }
            elsif ($key eq 'figure') 
            {
                push @string, visit_figure($child);
            }
            elsif ($key eq 'vspace') {}
            elsif ($key eq 'xref')   
            {
                push @string, $child->content;
            }
            else
            {
                printf STDERR ("ERROR: %s.%s is not handled\n", $node->key(), $key);
            }
            undef $child;
        }
    }
    if ($node->{'anchor'} =~ /^xsd_(\w+)/)
    {
        my $xsd = $XSD->{'xs:schema'}{'xs:element'}('name','eq',$1);

        if ($xsd)
        {
            push @string, "Schema:\n <pre>" . $xsd->data_pointer('nometagen' => 1) . '</pre>' if ($xsd);
        }
    }

    return join("\n", @string);
}

sub visit_list {
    my ($node) = @_;
    my @string = ();
    printf STDERR ("[%s]\n", $node->key());

    foreach my $child ($node->nodes())
    {
        my $key = $child->key();
        #print $key, "\n";

        if ($key eq 't')
        {
            push @string, sprintf("*%s\n", visit_t($child));
        }
        else
        {
            print STDERR $key, "\n";
        }
        undef $child;
    }


    return join("\n", @string);
}

sub visit_figure {
    my ($node) = @_;
    my @string = ();
    printf STDERR ("[%s]\n", $node->key());

    #print STDERR $node->{'alt'};
    #print STDERR $node->data_pointer;
    #my $alt = $node->{'alt'};
    #my $xsd = $XSD->{'xs:schema'}{'xs:element'}('name','eq',$alt);

    #print STDERR $xsd->data_pointer;
    #if ($xsd)
    #{
    #    push @string, "* UML:\n <pre>" . $node->{'artwork'} . '</pre>';
    #    push @string, "* Schema:\n <pre>" . $xsd->data_pointer . '</pre>' if ($xsd);
    #}
    #else
    #{
        push @string, "<pre>" . $node->{'artwork'} . '</pre>';
    #}

    return join("\n", @string);
}

sub visit_texttable {
    my ($node) = @_;
    my @string = ();
    printf STDERR ("[%s]\n", $node->key());

    my @cols = @{ $node->{'ttcol'} };
    my @c    = @{ $node->{'c'} };

    my $table_str = "";
    foreach $col (@cols)
    {
        #print STDERR "col", "\n";
        $table_str .= sprintf("!%s\n", $col->content);
    }

    my $num_cols = scalar(@cols);
    my $num_c    = scalar(@c);
    
    my $width = 300;
    for(my $i=0; $i<$num_c-$num_cols; $i+=$num_cols)
    {
        $c_str = join("||",@c[$i...$i+$num_cols-1]);
        $c_str =~ s/\n*//mg;
        $c_str =~ s/\s+/ /mg;

        $table_str .= "|-\n";
        $table_str .= "|" . $c_str . "\n";

        my $c_str_len = length($c_str);

        print STDERR $c_str_len, "\n";
        $width = ($c_str_len > $width and $c_str_len < 800) ? $c_str_len : $width;
    }

    my $table_header = "\n\n{| border=\"1\" width=\"$width\" style=\"margin: 100px\"\n";
    my $table_footer = "\n|}\n";
    push @string, $table_header . $table_str . $table_footer;

    #print STDERR $table_str;
    return join("\n", @string);
}



# vim: sw=4 ts=4 sts=4 expandtab
