CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -ggdb
LFLAGS=-lm
STRIP=strip
EXE_EXT=

revb:
	$(CC) $(CFLAGS) $^ -o $@ $(LFLAGS)
	$(STRIP) $@
	
include Makefile.common
