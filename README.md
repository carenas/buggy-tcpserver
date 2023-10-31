# buggy-tcpserver
An intentionally buggy server that can be used to mock failure scenarios in client/server applications

It uses a very simple line based protocol over a single TCP connection for added flexibility.

The protocol uses up to 8 ASCII character commands that are meant to be input (most likely interactively) from the client.

Both client and server are provided, but the server is designed in a way that should be also useful with a dumb client (ex: netcat or a telnet session) and therefore would include UNIX LF (`'\n'`) terminated responses.

Some command line options are available to change the application behaviour, but some commands are spected to be interpreted by the client. The server will provide a response to all commands that it knows about, but unlike a "real" server is meant to be run in the foreground just like the client.

All code is available under the repository LICENSE and can be built by invoking `make`. There is no installation process and while dependencies are minimal, you might need to add some modules to your perl interpreter to get the client running.
