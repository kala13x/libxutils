CFLAGS = -g -O2 -Wall
LIBS = -lxutils -lpthread -lm
LIBS += -lssl -lcrypto

EXECS = array \
		files \
		events \
		thread \
		statcov \
		strings \
		server \
		json \
		xtop \
		xlog \
		xcrypt \
		xpass \
		xhttp \
		xjson \
		xsrc \
		list \
		ntp \
		api

all:
	$(MAKE) $(EXECS)

files:
	$(CC) $(CFLAGS) -o files files.c $(LIBS)

server:
	$(CC) $(CFLAGS) -o server server.c $(LIBS)

statcov:
	$(CC) $(CFLAGS) -o statcov statcov.c $(LIBS)

xlog:
	$(CC) $(CFLAGS) -o xlog xlog.c $(LIBS)

thread:
	$(CC) $(CFLAGS) -o thread thread.c $(LIBS)

xtop:
	$(CC) $(CFLAGS) -o xtop xtop.c $(LIBS)

array:
	$(CC) $(CFLAGS) -o array array.c $(LIBS)

xhttp:
	$(CC) $(CFLAGS) -o xhttp xhttp.c $(LIBS)

strings:
	$(CC) $(CFLAGS) -o strings strings.c $(LIBS)

json:
	$(CC) $(CFLAGS) -o json json.c $(LIBS)

xjson:
	$(CC) $(CFLAGS) -o xjson xjson.c $(LIBS)

events:
	$(CC) $(CFLAGS) -o events events.c $(LIBS)

list:
	$(CC) $(CFLAGS) -o list list.c $(LIBS)

ntp:
	$(CC) $(CFLAGS) -o ntp ntp.c $(LIBS)

xsrc:
	$(CC) $(CFLAGS) -o xsrc xsrc.c $(LIBS)

xcrypt:
	$(CC) $(CFLAGS) -o xcrypt xcrypt.c $(LIBS)

xpass:
	$(CC) $(CFLAGS) -o xpass xpass.c $(LIBS)

api:
	$(CC) $(CFLAGS) -o api api.c $(LIBS)

.PHONY: install
install:
	install -m 0755 xcrypt /usr/bin/
	install -m 0755 xpass /usr/bin/
	install -m 0755 xjson /usr/bin/
	install -m 0755 xhttp /usr/bin/
	install -m 0755 xtop /usr/bin/
	install -m 0755 xsrc /usr/bin/

.PHONY: clean
clean:
	$(RM) $(EXECS) $(OBJS)
