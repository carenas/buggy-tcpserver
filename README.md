# buggy-tcpserver
An intentionally buggy server that can be used to mock failure
scenarios in client/server applications. Do not use any of this code
for real use and instead rely in a properly working framework.

It uses a very simple line based protocol over a single TCP connection
for added flexibility, instead of HTTP.

The protocol uses ASCII character commands between 2 and 8 bytes long
that are meant to be input (most likely interactively) from the client.

Both client and server are provided, but the server is designed in a
way that should be also useful with a dumb client (ex: netcat or a
telnet session) and therefore would detect and sent back line endings
for some responses.

Some command line options are available to change the application
behaviour, but some commands are expected to be interpreted by the
provided client.

The server will provide a response to most of the commands that it
knows about, but unlike a "real" server is meant to be run in the
foreground just like the client.

All code is available under the repository LICENSE and can be built by
invoking `make`. There is no installation process and dependencies are
kept minimal for portability and are optional (althought the use of
IO::Socket::Timeout in the client is encouraged).

This code is not meant to be bug free, but hopefully most of the bugs
are documented. It is also too verbose and repetitive so it can be
tweaked to match the specific scenario you are observing, and has some
probably odd defaults and bugs selected as it was born as an attempt to
replicate problems observed in one specific case[1], hence also the
strange focus on making sure all sockets were closed correctly.

It is also not meant to be complete, as more commands or different bugs
might be needed for different scenarios.

[1] https://stackoverflow.com/questions/77355636/close-wait-tcp-states-despite-closed-file-descriptors/
