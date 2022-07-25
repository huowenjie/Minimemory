CFLAG=-std=c99

main:main.o mem.o mem_page.o link.o
	gcc $^ -o $@
main.o:main.c mem.o mem_page.o link.o
	gcc -g -c main.c -o $@ -I. $(CFLAG)
mem.o: mem.c mem_page.o link.o mem.h mem_page.h link.h
	gcc -g -c mem.c -o $@ -I. $(CFLAG)
mem_page.o: mem_page.c link.o mem_page.h link.h
	gcc -g -c mem_page.c -o $@ -I. $(CFLAG)
link.o: link.c link.h
	gcc -g -c link.c -o $@ -I. $(CFLAG)

.PHONY: clean
clean:
	rm -f *.o main