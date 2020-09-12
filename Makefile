CC      = i586-pc-msdosdjgpp-gcc
CFLAGS  = -Os -Wall -Wextra
LDFLAGS = -s
LDLIBS  =
DOSBOX  = dosbox

lawn.exe: lawn.c back.h gameover.h goml.h gun.h intro.h monster.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ lawn.c $(LDLIBS)

play.exe: play.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ play.c $(LDLIBS)

run: lawn.exe
	$(DOSBOX) -conf lawn.conf lawn.exe >/dev/null

clean:
	rm -f lawn.exe play.exe
