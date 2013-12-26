For information about this port of ssh/2, see the doc/ssh/README.OS2 file.

To build ssh 1.2.30 for OS/2, follow the instructions:

	- Download and unpack ssh 1.2.30 sources
	- Apply the diff found in this directory
	- Copy the contents of the os2/ directory (along with the
	  directory itself) under the ssh directory (i.e. it should
	  become ssh-1.2.30/os2)

Well, its not that easy although its not that hard. First, you should have some
tools installed:

	-> Some Unix-like shell (my port of bash, for example) (or ash)
	-> GNU file utilites
	-> GNU autoconf for OS/2
	-> EMX of course (port done with pgcc 2.95.3)
	-> A whole lot of other tools that you won't know you need until
	   you find an `bad command or file name' or similar.

Now go to root ssh directory and type:

	cd gmp-2.0.2-ssh-2
	{ba}sh autoconf
	cd ..
	{ba}sh autoconf
	{ba}sh ./configure --prefix=/emx

(well, the --prefix is not strictly required, but just for fun ...)
Now prepare to wait for a long time watch...

The configure command will configure the ssh itself, the zlib and the gmp
(GNU multiprecision library, that is). Now if you run on a Pentium and higher
(who doesn't) go to gmp-2.0.2-ssh-2/mpn/x86/pentium and copy all those .S
files over those that configure copied from mpn/x86/ into mpn/.

Now the 'fix' step. For some dumb reasons autoconf fails to find out some
things about EMX. Load Makefile into your favorite editor and remove
socketpair.o from LIBOBJS in Makefile (it is not used and won't compile
anyway...). Also add the line

	vpath %.c os2

somewhere at the top of Makefile. Also if you don't have the `crypt' library,
but you do have the UFC library, you should change -lcrypt into -lufc.
Also please check for -Zbin-files switch to be present on the LDFLAGS line.
The autoconf that I have automatically adds it, but who knows what version
you got...

Now if you want dependencies and you do have makedepend or makedep (if you
have makedep, replace makedepend with makedep in makefile), run:

	make depend srcdir=os2

Don't bother about the errors about missing X11 headers, its ok.

If you have zlib pre-installed (like me) you may want to empty the ZLIBDEP
variable.

Finally, run make in the gmp-2.0.2-ssh-2 directory. You can change the makefile
since usually CFLAGS and LFLAGS get dumb values (I've used CFLAGS =
"-s -O6 -funroll-loops" with pgcc compiler). If it works, you should end up
with libgmp.a and libmp.a.

Now, go back to ssh root dir. Change the CFLAGS and LFLAGS (I've used CFLAGS =
"-s -O6 -unroll-loops" and LDFLAGS = "-s -Zexe -Zcrtdll". Type make.

Congratulations, you did it.
