PROG=xvcpi
CFLAGS=-O3

all: $(PROG)


$(PROG): $(PROG).o
	$(CC) -o $(PROG) $< -lbcm_host

clean:
	rm -f $(PROG) *.o
