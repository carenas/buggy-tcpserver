#!/usr/bin/perl

# SPDX-License-Identifier: BSD-2-Clause

use strict;
use warnings;

use POSIX;
use Errno qw(ETIMEDOUT);
use IO::Socket::INET;

my $timeout_available = eval {
    require IO::Socket::Timeout;
    IO::Socket::Timeout->import();
    5;
};

sub ltrim {
    my $s = shift;
    $s =~ s/^[^\S\n]+//gm unless !defined($s);
    $s = undef unless length($s);
    return $s;
}

$| = 1;

my $exit = 0;
my $pipe;

$SIG{INT} = sub { $pipe = 0; $exit = 1 };
$SIG{PIPE} = sub { $pipe = 1 };

my $connect_failure = "none";
my $big_on_connect;
my $debug = 0;

while (my $arg = shift) {
    if ($arg =~ /.*connect.*=(\w+)/) {
        $connect_failure = $1;
    } elsif ($arg =~ /.*big/) {
        $big_on_connect = 1;
    } elsif ($arg =~ /.*debug/) {
        $debug = 1;
    } elsif ($arg =~ /.*help/) {
        print "$0 <options>\n";
        print ltrim(<<"USAGE");
        A perl implementation of a client for the buggy tcp_server
        with optional support for timeouts and other goodies.

        --connect=close|reset        "ping" at connect, end FIN/RST if fail
        --big                        "big" at connect
        --debug                        enable debugging messages

USAGE
        print "See code for details\n";
        exit 0;
    }
}

my $socket = new IO::Socket::INET (
    PeerHost => "127.0.0.1",
    PeerPort => 7777,
    Proto => "tcp",
    Timeout => 5,
) or die "$IO::Socket::errstr\n";

my $close_on_exit = 1;
my $early_passive = 0;
my $has_timeout = 0;
my $short_pipe = 0;
my $shutdown = -1;
my $init = "";
my $buffer;
my $n;
my $id = $socket->sockport();

if ($connect_failure =~ /close|reset/ || defined($big_on_connect)) {
    my $fail = 0;
    if (defined($big_on_connect)) {
        $buffer = 'a' x PIPE_BUF;
    } else {
        $buffer = "ping!";
    }
    $n = $socket->send($buffer);
    if ($n == 5) {
        if ($timeout_available) {
            IO::Socket::Timeout->enable_timeouts_on($socket);
            $socket->read_timeout($timeout_available);
            $has_timeout = 1;
        } else {
            warn "might require ctrl+c if connection blocks"
        }
        $buffer = "";
        $socket->recv($buffer, 5);
        $fail = length($buffer) != 5;
    } elsif ($n == PIPE_BUF) {
        if ($connect_failure =~ /close|reset/) {
            IO::Socket::Timeout->enable_timeouts_on($socket);
            $socket->read_timeout(50);
            $has_timeout = 1;
        }
        $socket->recv($buffer, PIPE_BUF);
        print "DEBUG: recv(".length($buffer).")\n" if $debug;
        if (length($buffer) == PIPE_BUF) {
            $socket->recv($buffer, PIPE_BUF);
            print "DEBUG: recv(".length($buffer).")\n" if $debug;
        } elsif ($debug) {
            print "DEBUG: ".scalar($!)."\n";
        }
        if ($connect_failure =~ /close|reset/) {
            $fail = length($buffer) != PIPE_BUF;
            $socket->disable_timeout();
        }
    } else {
        $fail = 1;
    }
    if ($fail) {
        $socket->setsockopt(SOL_SOCKET, SO_LINGER, pack("II", 1, 5)) if $connect_failure eq "reset";
        $socket->shutdown(SHUT_WR);
        $socket->close();
        die "$id: $connect_failure\n";
    }
    $init = "big" if length($buffer) == PIPE_BUF;
}

print "$id: $init"."connect\n";

while (<STDIN>) {
    my $size = 8;
    $buffer = "";

    chomp;
    $close_on_exit = 0 if /die/;
    $size = PIPE_BUF if /big/; # BUG: might need to be bigger
    if (/passive/) {
        $early_passive = 1;
        next;
    }
    if (/short/) {
        $short_pipe = 1;
        next;
    }
    if (/shut(?:down)?\s*(\d)/a) {
        $shutdown = $1;
        print "INVALID\n" if $shutdown > 2 && $debug;
        next;
    }
    if (/ti?me?out(\s*[\d.]*)?/a) {
        my $v = ltrim($1);
        if ($timeout_available) {
            if (!$has_timeout) {
                IO::Socket::Timeout->enable_timeouts_on($socket);
            }
            if (defined($v)) {
                $timeout_available = $v;
            }
            # TODO: write timeouts might be also useful
            $socket->read_timeout($timeout_available);
            $has_timeout = 1;
        } else {
            print "UNAVAIL\n" if $debug;
        }
        next;
    }
    last if $exit || /bye|die/;

    # BUG: might need to use sysread and cousins for reliable timeouts
    $socket->send($_);
    $socket->shutdown($shutdown) if $shutdown >= 0 && $early_passive && !$short_pipe;
    $socket->recv($buffer, $size); # BUG: timeout might go undetected
    if (!length($buffer)) {
        if ($timeout_available && $has_timeout && $socket->timeout_enabled()) {
            print "timeout!\n" unless $pipe;
        }
        print "pipe!\n" if $pipe;
        last;
    }
    if ($size > 8) {
        if ($short_pipe) {
            $socket->shutdown($shutdown) if $shutdown >= 0 && $early_passive;
        }
        $socket->recv($buffer, $size);
        print "big!\n" if length($buffer) == PIPE_BUF;
    } else {
        chomp $buffer;
        print "$buffer\n";
    }
}
$socket->shutdown($shutdown) if $shutdown >= 0 && !$early_passive;
$socket->close() if $close_on_exit;
