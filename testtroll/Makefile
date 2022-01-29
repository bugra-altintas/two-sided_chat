# Makefile for client and server

CC = gcc
OBJCLI = totroll.c 
OBJSRV = fromtroll.c
CFLAGS = 
# setup for system
LIBS =

all: totroll fromtroll

totroll:	$(OBJCLI)
	$(CC) $(CFLAGS) -o $@ $(OBJCLI) $(LIBS)

fromtroll:	$(OBJSRV)
	$(CC) $(CFLAGS) -o $@ $(OBJSRV) $(LIBS)

clean:
	rm totroll fromtroll
