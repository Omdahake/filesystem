CC = gcc
CFLAGS = -Wall -g -D_FILE_OFFSET_BITS=64

SRCS = main.c virt_disk.c fsops.c dir.c
OBJS = $(SRCS:.c=.o)

all: virt_dsk fuse_mount

virt_dsk: $(OBJS)
		$(CC) $(CFLAGS) -o virt_dsk $(OBJS)

fuse_mount: fuse_bridge.o virt_disk.o fsops.o dir.o
		$(CC) $(CFLAGS) -o fuse_mount fuse_bridge.o virt_disk.o fsops.o dir.o -lfuse -lpthread

%.o: %.c
		$(CC) $(CFLAGS) -c $< -o $@

clean:
		rm -f *.o virt_dsk fuse_mount
		
