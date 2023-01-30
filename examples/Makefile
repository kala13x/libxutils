CFLAGS = -g -O2 -Wall -D_XUTILS_USE_SSL
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
	jwt \
	rsa \
	api

all: $(EXECS)

$(EXECS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

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
	$(RM) $(EXECS)
