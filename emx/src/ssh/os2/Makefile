O = .obj
A = .lib

CC = gcc -c
CFLAGS = -s -Zomf -Zmt -O6 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

LD = gcc
LDFLAGS = -s -Zomf -Zmt -Zlinker /PM:VIO -Zcrtdll -L. -lgcc
LIBS = so32dll$A

.SUFFIXES:
.SUFFIXES: $O $A .exe .cpp

all: tnpipe.exe user.exe

clean:
	rm -f *$O *$A *.exe

%$O: %.c
	$(CC) $(CFLAGS) -o $@ $<
%$O: %.cpp
	$(CC) $(CXXFLAGS) -o $@ $<

tnpipe.exe: tnpipe$O $(LIBS)
	$(LD) -o $@ $^ $(LDFLAGS)

user.exe: user$O
	$(LD) -o $@ $^ $(LDFLAGS) -lufc -liberty_s -lgcc

$(LIBS):
	emximp -o $@ $*.imp
