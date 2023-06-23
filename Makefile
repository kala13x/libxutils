####################################
# Automatically generated by SMake #
# https://github.com/kala13x/smake #
####################################

CFLAGS = -g -O2 -Wall -D_XUTILS_DEBUG -D_XUTILS_USE_SSL -D_XUTILS_USE_GNU
CFLAGS += -I./src -I./src/sys -I./src/net -I./src/data -I./src/crypt
LIBS = -lpthread -lssl -lcrypto
NAME = libxutils.a
ODIR = ./obj
OBJ = o

OBJS = addr.$(OBJ) \
	aes.$(OBJ) \
	api.$(OBJ) \
	array.$(OBJ) \
	base64.$(OBJ) \
	crc32.$(OBJ) \
	crypt.$(OBJ) \
	event.$(OBJ) \
	hash.$(OBJ) \
	hmac.$(OBJ) \
	http.$(OBJ) \
	jwt.$(OBJ) \
	list.$(OBJ) \
	map.$(OBJ) \
	md5.$(OBJ) \
	mdtp.$(OBJ) \
	ntp.$(OBJ) \
	rsa.$(OBJ) \
	rtp.$(OBJ) \
	sha1.$(OBJ) \
	sha256.$(OBJ) \
	sock.$(OBJ) \
	sync.$(OBJ) \
	thread.$(OBJ) \
	ws.$(OBJ) \
	xbuf.$(OBJ) \
	xcli.$(OBJ) \
	xcpu.$(OBJ) \
	xfs.$(OBJ) \
	xjson.$(OBJ) \
	xlog.$(OBJ) \
	xsig.$(OBJ) \
	xstr.$(OBJ) \
	xtime.$(OBJ) \
	xtop.$(OBJ) \
	xtype.$(OBJ) \
	xver.$(OBJ)

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
	@install -m 0755 $(ODIR)/$(NAME) $(INSTALL_BIN)/
	@test -d $(INSTALL_INC) || mkdir -p $(INSTALL_INC)
	@cp -r ./src/crypt/*.h $(INSTALL_INC)/
	@cp -r ./src/data/*.h $(INSTALL_INC)/
	@cp -r ./src/net/*.h $(INSTALL_INC)/
	@cp -r ./src/sys/*.h $(INSTALL_INC)/
	@cp -r ./src/*.h $(INSTALL_INC)/

.PHONY: clean
clean:
	$(RM) $(ODIR)/$(NAME) $(OBJECTS)
