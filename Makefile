#
# Generic rules tested @Linux ubuntu 3.8.0-29-generic #42~precise1-Ubuntu
#

TARGET	= quasar
CC	= gcc
CFLAGS	= -g -O3 -Wall -pthread
LIBS	= -lrt

HEADERS = $(wildcard *.h)
OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))

.PHONY: all clean

all: $(TARGET) clean

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	-rm -f *.o
