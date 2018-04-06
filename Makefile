all:
	cc main.c -Wall -o find

debug:
	cc main.c -O1 -Wall -g -fsanitize=address -o find-debug
