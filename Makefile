build:
	gcc -o overlord overlord.c

install: build
	cp overlord /usr/bin/overlord
