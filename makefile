
WORKDIR=.
VPATH = ./src

CC=gcc
CFLGS= -Wall -g -I$(WORKDIR)/inc/
LIBFLAG = -L$(HOME)/lib

BIN = keymngclient keymngserver

all:$(BIN)
# myipc_shm.o keymng_shmop.o keymng_dbop.o
# -lclntsh  -licdbapi

keymngclient:keymngclient.o keymnglog.o keymngclientop.o myipc_shm.o keymng_shmop.o keymng_dbop.o
	$(CC) $(LIBFLAG) $^ -o $@ -litcastsocket -lmessagereal -lclntsh  -licdbapi

keymngserver:keymngserver.o keymnglog.o keymngserverop.o myipc_shm.o keymng_shmop.o keymng_dbop.o
	$(CC) $(LIBFLAG) $^ -o $@ -lpthread -litcastsocket -lmessagereal -lclntsh  -licdbapi

%.o:%.c
	$(CC) $(CFLGS) -c $< -o $@

.PHONY:clean all
clean:
	-rm -f *.o $(BIN)
