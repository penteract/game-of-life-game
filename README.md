# Ludum Dare 46

Server and client code for playing the game of life. When a cell is born, if 2 out of the 3 neighbours share a colour, the cell becomes that colour. Otherwise it is born black.

Made in 48 hours for ludum dare 46 [link](https://ldjam.com/events/ludum-dare/46/imojc)

to build: gcc main.c
to run: ./a.out


Some base code came from another game I've been working on [set](https://github.com/penteract/badserver/). This was code for working with sockets, reading files and making HTTP responses.

Missing features:
-  proper touch input
-  resizing with the number of active players
-  colour according to something other than socket number
-  Use a perceptually uniform colour space
-  iOS input failing completely?
-  shrink zoom limits, or improve rendering efficiency
-  Draw gray selected cell in the right places

I may have spent too long writing base64 encoding/decoding functions.

The Hammer javascript library is here because I tried to use it, but didn't in the end. Removing the file would break the C code. Otherwise, vanilla js (only tested in firefox 75) and the POSIX standard library are used.
