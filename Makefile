CC = gcc

EXECUTABLE = virt_dsk
SOURCES = main.c virt_disk.c
#OBJECTS = $(SOURCES:.c=.o)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(SOURCES) -o $(EXECUTABLE)


clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

