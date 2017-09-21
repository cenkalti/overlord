build:
	gcc -pthread -o overlord overlord.c

install: build
	cp overlord /usr/bin/overlord
