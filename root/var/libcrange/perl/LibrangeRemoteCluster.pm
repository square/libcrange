package LibrangeRemoteCluster;

use strict;
use warnings;

use Libcrange;
use LWP::Simple;

sub functions_provided {
    return qw/cluster/;
}

sub cluster {
    my ($rr, $range) = @_;

    my $server = Libcrange::get_var($rr, "range_server");
    $server = "range" unless $server;

    return map { split /\n/, get("http://$server/range/list?%$_") } @$range;
}

1;
