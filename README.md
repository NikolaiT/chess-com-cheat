chess-com-cheat
===============

Library that hooks into PR_Write() and PR_Read() in firefox processes and manipulates WebSocket Messages to cheat on chess.com. This means
you can cheat with this code on chess.com. But you'll need Linux and Firefox. You could also run Linux in a virtual machine of course, if you
don't want to change OS.

### Known issues

Because I am not a good C Programmer there might be several memory leaks in the library. Sometimes when the library hooks into firefox
the process just crashes (Segmentation fault). I assume it's because the tremendous processing of packets that are pushed through 
PR_Write() and PR_Read() and I have a bad approach to parse the correct ones out of the stream.

### Installation and requirements

You might want to clone this repository and install gcc and its consortes development tools. You need some kind of Linux (or other UNIX variant) in order
to use this cheat. Furthermore you need firefox, because this cheat only works in combination with firefox (it specifically hooks low level
networking functions from firefox).

After clonging the repository and when you fulfill all the above requirements, fire up a shell and follow me with the next steps:

Around line 55 in the source code of cheat_lib.com, there is a line `#define PLAYER_UID "SomePlayerName"`. Replace SomePlayerName with your chess.com 
user name and then save the C file again. Next step is to compile the source code into a shared library with the followig command (note that you have
to be in the exact same directory as the C source file you just edited!):

`gcc -Wall -shared -ggdb3 -fPIC -ldl -ljsmn -L$PWD/jsmn/ -o libpwh.so cheat_lib.c jsmn/jsmn.c`

This should give you a shared library named `libpwh.so`. This file is now dynamically loaded in the firefox process space with the LD_PRELOAD trick, which is
our next step. So now, while you are still in the same directory, fire up this command in the shell:

`export LD_PRELOAD=$PWD/libpwh.so`

this sets the LD_PRELOAD environment variable to the shared library you just compiled. Now we are ready to go: Start firefox with `firefox` in the same
bash terminal you just used. Then firefox starts up. Now login to live.chess.com and start a game. Make random moves as soon as you can and you will see
that the game seems to be totally messed up. This is because the shared library injects engine moves into the packets, which confuses the adobe frontend gui.
Nevertheless, after some seconds, the game updates and you see the real engine moves that were made!








