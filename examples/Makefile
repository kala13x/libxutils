####################################
# Automatically generated by SMake #
# https://github.com/kala13x/smake #
####################################

CFLAGS = -g -O2 -Wall -D_XUTILS_DEBUG -D_ASSERT_TIMED
LIBS = -lpthread
LD_LIBS = ../build/libxutils.a

ifeq ($(XUTILS_USE_SSL),y)
CFLAGS += -D_XUTILS_USE_SSL
LIBS += -lssl -lcrypto
endif

CFLAGS += -I../src \
	-I../src/crypt \
	-I../src/data \
	-I../src/net \
	-I../src/sys

EXECS = array \
	async-server \
	async-client \
	basic-server \
	http-server \
	ws-server \
	ws-client \
	statcov \
	strings \
	events \
	thread \
	files \
	json \
	xlog \
	list \
	ntp \
	jwt \
	rsa

all: $(EXECS)

$(EXECS): %: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LD_LIBS) $(LIBS)

.PHONY: clean
clean:
	$(RM) $(EXECS)
