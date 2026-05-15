CC     = /bin/cc
CFLAGS = -nostdlib -nostdinc -I/include -I/lib/tcc/include
LIBS   = /lib/crt.o /lib/libc.a /lib/tcc/libtcc1.a
OUT    = /mnt/test_compile_pipeline

SRC    = /test/c_sample/test_compile_pipeline.c

$(OUT): $(SRC)
	$(CC) $(CFLAGS) $(SRC) $(LIBS) -o $(OUT)

.PHONY: clean run
run: $(OUT)
	$(OUT)

clean:
	rm -f $(OUT)
