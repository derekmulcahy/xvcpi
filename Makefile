PROG=xvcpi
CFLAGS=-std=gnu99 -O3

all: $(PROG)

$(PROG): $(PROG).o
	$(CC) -o $(PROG) $<

clean:
	rm -f $(PROG) *.o
