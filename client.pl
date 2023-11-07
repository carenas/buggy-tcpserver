#!/usr/bin/perl

# SPDX-License-Identifier: BSD-2-Clause

my $hostname = "127.0.0.1";
my $port = 7777;
my $connect_timeout = 5;
my $read_timeout = 5;
my $connect_failure = "none";

# Most variables below are not configuration variables

use strict;
use warnings;

use POSIX;
use Errno qw(ETIMEDOUT);
use IO::Socket::INET;

my $timeout_available = eval {
    require IO::Socket::Timeout;
    IO::Socket::Timeout->import();
    $read_timeout;
};

sub ltrim {
    my $s = shift;
    $s =~ s/^[^\S\n]+//gm;
    $s = undef unless length($s);
    return $s;
}

sub synopsis_format {
    foreach (@_) {
        s/^--/\t--/gm;
    }
    return @_;
}

$| = 1;

my $exit = 0;
my $pipe;

$SIG{INT} = sub { $pipe = 0; $exit = 1 };
$SIG{PIPE} = sub { $pipe = 1 };

my $big_on_connect;
my $debug = 0;

while (my $arg = shift) {
    if ($arg =~ /.*connect[^_]*=(\w+)/) {
        $connect_failure = $1;
    } elsif ($arg =~ /.*connect\w*_timeout=([.\d]+)/a) {
        $connect_timeout = $1;
    } elsif ($arg =~ /.*read_timeout=([.\d]+)/a) {
        die "Not supported" unless defined($timeout_available);
        $read_timeout = $1;
    } elsif ($arg =~ /.*server=([\w.]+)((?<=:)\d+)?/a) {
        # BUG: no support for IPv6 address
        $hostname = $1;
        $port = $2 if defined($2);
    } elsif ($arg =~ /.*big/) {
        $big_on_connect = 1;
    } elsif ($arg =~ /.*debug/) {
        $debug = 1;
    } elsif ($arg =~ /.*help/) {
        print "$0 <options>\n";
        print synopsis_format(ltrim(<<"SYNOPSIS"));
        A perl implementation of a client for the buggy tcp_server
        with optional support for timeouts and other goodies.

        --server=<host[:port]>    defaults to $hostname:$port
        --connect_timeout=<value> defaults to $connect_timeout seconds
        --read_timeout=<value>    defaults to $read_timeout seconds
        --connect=close|reset     "ping" at connect, send FIN/RST if timeout
        --big                     "big" at connect
        --debug                   enable debugging messages

SYNOPSIS
        print "See code for details\n";
        exit 0;
    }
}

my $socket = new IO::Socket::INET (
    PeerHost => $hostname,
    PeerPort => $port,
    Proto => "tcp",
    Timeout => $connect_timeout,
) or die "$hostname:$port $@\n";

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
    my $big = defined($big_on_connect);
    my $fail = 0;
    if ($big) {
        $buffer = 'a' x PIPE_BUF;
    } else {
        $buffer = "ping!";
    }
    $n = $socket->send($buffer);
    if ($n == 5 && !$big) {
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
            $socket->read_timeout($read_timeout);
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
    my $expect_empty_response;
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
    if (/linge?r\s*(\d+)/a) {
        my $linger_timeout = $1;
        $socket->setsockopt(SOL_SOCKET, SO_LINGER, pack("II", 1, $linger_timeout));
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
    $expect_empty_response = /close/;
    last if $exit || /bye|die/;

    # BUG: might need to use sysread and cousins for reliable timeouts
    $socket->send($_);
    $socket->shutdown($shutdown) if $shutdown >= 0 && $early_passive && !$short_pipe;
    $socket->recv($buffer, $size); # BUG: timeout might go undetected
    if (!length($buffer)) {
        print "pipe!\n" if $pipe;
        last if $expect_empty_response;
        if ($timeout_available && $has_timeout && $socket->timeout_enabled()) {
            print "timeout!\n" unless $pipe;
        }
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
