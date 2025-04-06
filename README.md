# minetd
A minimal and transparent Minecraft reverse proxy. Useful if you've got more domains than ports, or can't be bothered to open multiple ports.  
It can currently only forward to `127.0.0.1`, though I'll probably add arbitrary IP address support eventually.

## Dependencies
I'm actually cheating and using `socat` under the hood to handle the actual proxying, so it's kind of required for this project to work.

## Usage
Just run it, with the public port as the only argument. Keep in mind that terminating `minetd` will also terminate all children, and thus all connections.
`minetd <port number>`

## Configuration
### Location
`minetd` will look for a `minetd.conf` file in the current directory first, then in `$XDG_CONFIG_HOME/minetd/` (or `$HOME/.config/minetd/` if `$XDG_CONFIG_HOME` is not set), then `/etc/minetd/minetd.conf`.  
Only the first found file is loaded.
### Syntax
A valid configuration line is composed of a server's desired hostname, followed by any number of space characters (not tabs), followed by a port number, and it is, at most, 260 characters long.  
Empty lines, or lines starting with `#` or `//`, are ignored.
### Reloading
The configuration can be reloaded, without terminating, by sending `minetd` a `SIGHUP`.
