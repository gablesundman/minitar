CFLAGS = -Wall -Werror -g
CC = gcc $(CFLAGS)
SHELL = /bin/bash
CWD = $(shell pwd | sed 's/.*\///g')
AN = proj1

minitar: minitar_main.c file_list.o minitar.o
	$(CC) -o minitar minitar_main.c file_list.o minitar.o -lm

file_list.o: file_list.h file_list.c
	$(CC) -c file_list.c

minitar.o: minitar.h minitar.c
	$(CC) -c minitar.c

clean:
	rm -f *.o minitar
