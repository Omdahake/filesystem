CC = gcc

EXECUTABLE = virt_dsk
SOURCES = test.c virt_disk.c
OBJECTS = $(SOURCES:.c=.o)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(EXECUTABLE)


clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

