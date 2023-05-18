
CFLAGS = -g -O2 -Wall
LIBS = -lxutils -lpthread

ifeq ($(XUTILS_USE_SSL),y)
CFLAGS += -D_XUTILS_USE_SSL
LIBS += -lssl -lcrypto
endif

EXECS = array \
	http-server \
	tcp-server \
	ws-server \
	ws-client \
	events \
	files \
	thread \
	statcov \
	strings \
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
	rsa

all: $(EXECS)

$(EXECS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LIBS)

.PHONY: install
install:
	install -m 0755 xcrypt /usr/local/bin/
	install -m 0755 xpass /usr/local/bin/
	install -m 0755 xjson /usr/local/bin/
	install -m 0755 xhttp /usr/local/bin/
	install -m 0755 xtop /usr/local/bin/
	install -m 0755 xsrc /usr/local/bin/

.PHONY: clean
clean:
	$(RM) $(EXECS)