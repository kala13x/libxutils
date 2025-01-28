####################################
# Automatically generated by SMake #
# https://github.com/kala13x/smake #
####################################

CFLAGS = -D_XUTILS_USE_SSL -g -O2 -Wall -D_XUTILS_DEBUG -D_XUTILS_USE_GNU -D_ASSERT_TIMED
CFLAGS += -I./src/crypt -I./src/data -I./src/net -I./src/sys -I./src
LIBS = -lssl -lcrypto -lpthread
NAME = libxutils.a
ODIR = ./build
OBJ = o

OBJS = xver.$(OBJ) \
    aes.$(OBJ) \
    base64.$(OBJ) \
    crc32.$(OBJ) \
    crypt.$(OBJ) \
    hmac.$(OBJ) \
    md5.$(OBJ) \
    rsa.$(OBJ) \
    sha256.$(OBJ) \
    sha1.$(OBJ) \
    array.$(OBJ) \
    hash.$(OBJ) \
    json.$(OBJ) \
    jwt.$(OBJ) \
    list.$(OBJ) \
    map.$(OBJ) \
    buf.$(OBJ) \
    str.$(OBJ) \
    addr.$(OBJ) \
    event.$(OBJ) \
    http.$(OBJ) \
    mdtp.$(OBJ) \
    ntp.$(OBJ) \
    rtp.$(OBJ) \
    sock.$(OBJ) \
    api.$(OBJ) \
    ws.$(OBJ) \
    sync.$(OBJ) \
    pool.$(OBJ) \
    thread.$(OBJ) \
    type.$(OBJ) \
    cli.$(OBJ) \
    cpu.$(OBJ) \
    mon.$(OBJ) \
    log.$(OBJ) \
    sig.$(OBJ) \
    xfs.$(OBJ) \
    xtime.$(OBJ) \

OBJECTS = $(patsubst %,$(ODIR)/%,$(OBJS))
INSTALL_INC = /usr/local/include/xutils
INSTALL_BIN = /usr/local/lib
VPATH = ./src:./src/sys:./src/net:./src/data:./src/crypt

.c.$(OBJ):
	@test -d $(ODIR) || mkdir -p $(ODIR)
	$(CC) $(CFLAGS) -c -o $(ODIR)/$@ $< $(LIBS)

$(NAME):$(OBJS)
	$(AR) rcs -o $(ODIR)/$(NAME) $(OBJECTS)

.PHONY: install
install:
	@test -d $(INSTALL_BIN) || mkdir -p $(INSTALL_BIN)
	install -m 0755 $(ODIR)/$(NAME) $(INSTALL_BIN)/
	@test -d $(INSTALL_INC) || mkdir -p $(INSTALL_INC)
	cp -r ./src/crypt/*.h $(INSTALL_INC)/
	cp -r ./src/data/*.h $(INSTALL_INC)/
	cp -r ./src/net/*.h $(INSTALL_INC)/
	cp -r ./src/sys/*.h $(INSTALL_INC)/
	cp -r ./src/*.h $(INSTALL_INC)/

.PHONY: clean
clean:
	$(RM) $(ODIR)/$(NAME) $(OBJECTS)
