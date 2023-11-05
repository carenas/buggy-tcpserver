# buggy-tcpserver
An intentionally buggy server that can be used to mock failure
scenarios in client/server applications.

> [!WARNING]
> Do not use any of this code for real use and instead rely in a properly working framework.

## Protocol and Design

It uses a very simple line based protocol over a single TCP connection
for added flexibility, instead of HTTP.  Additionally, it keeps the connection
open by default (as done since stateful HTTP) for compatibility.

The protocol uses between 2 and 8 byte long commands encoded as ASCII (including
optional spaces and line endings, if needed) that are meant to be input
(most likely interactively) from the client.

The provided client will send most commands AS-IS to the server but will
also process some internally to change its behavior.  There is minimal
validation and any abuse will likely result in crashes or other errors by design.

The server is designed in a way that should be also useful with a dumb client
(ex: netcat or a telnet session) and therefore would detect and sent back line
endings for some responses.

Some command line options are available to change the application behavior
(both in the client and server), including `--help` and `--debug` which could
provide additional useful information.

The server will provide a response to most of the commands that it
knows about, but unlike a "real" server is meant to be run in the
foreground just like the client, and will print to standard output (which
is shared with any child) its state.

The client always expects a response from the server, and therefore the lack of
one should be expected if the server fails, this includes processing of an
unknown command as well, by design.

All code is available under the repository LICENSE and can be built by
invoking `make`. There is no installation process and dependencies are
kept minimal, for portability, as well as being optional (although the use
of IO::Socket::Timeout in `client.pl` is encouraged).

## Using and Extending

This code is not meant to be bug free, but hopefully most of the bugs and smells
are documented. It is also too verbose and repetitive so it can be
tweaked to match the specific scenario you are observing, and has some
probably odd defaults and bugs selected as it was born as an attempt to
replicate problems observed in one specific
[case](https://stackoverflow.com/questions/77355636/close-wait-tcp-states-despite-closed-file-descriptors/).
Hence also the strange focus on making sure all sockets were closed correctly.

It is meant to be modified to fit your use case though, so it was cleaned up
slightly from the original throwaway version, even if it might still keep some
of that original "ugliness" (like short, almost meaningless variable names and
extremely long lines due to the lack of modularization).

It is also not meant to be complete (most known TODOs also documented), as more
commands or different bugs will be needed by different scenarios.

Implementing alternative clients is encouraged, as well as servers that
might implement a different solution that matches better the one you are
troubleshooting.

A server based on the same framework/language than the problematic one will
be even better.
