#!/bin/bash

# Enable particular functionality
sourceList="${SOURCE_DIR}/xver.c"
extension=""
sslFlags=""
sslLibs=""

if [[ $1 == "make" ]]; then
    [ ! -f "./misc/Makefile.tmp" ] && \
        echo "Makefile template file is not found" && exit 1

    extension='$(OBJ) \\'

elif [[ $1 == "cmake" ]]; then

    [ ! -f "./misc/CMakeLists.tmp" ] && \
        echo "CMakeLists template file is not found" && exit 1

    extension="c"

else
    echo "Invalid make tool";
    exit 1;
fi

for arg in "$@"; do
    if [[ $arg == "--ssl" ]]; then
        sslFlags="-D_XUTILS_USE_SSL"
        sslLibs="-lssl -lcrypto"
    fi
done

. $(dirname "$0")/xutils.conf
SOURCE_DIR=./src

enable_aes() {
    USE_AES=y
}

enable_base64() {
    USE_BASE64=y
}

enable_crc32() {
    USE_CRC32=y
}

enable_array() {
    USE_ARRAY=y
}

enable_list() {
    USE_LIST=y
}

enable_rtp() {
    USE_RTP=y
}

enable_sync() {
    USE_SYNC=y
}

enable_xsig() {
    USE_XSIG=y
}

enable_md5() {
    USE_MD5=y
    enable_xstr
}

enable_sha256() {
    USE_SHA256=y
    enable_xstr
}

enable_sha1() {
    USE_SHA1=y
    enable_xstr
}

enable_hash() {
    USE_HASH=y
    enable_list
}

enable_xbuf() {
    USE_XBUF=y
    enable_xstr
}

enable_xstr() {
    USE_XSTR=y
    enable_array
}

enable_thread() {
    USE_THREAD=y
    enable_sync
}

enable_xtime() {
    USE_XTIME=y
    enable_xstr
}

enable_xtype() {
    USE_XTYPE=y
    enable_xstr
}

enable_ws() {
    USE_WS=y
    enable_xbuf
}

enable_rsa() {
    USE_RSA=y
    enable_xfs
    enable_sha256
}

enable_map() {
    USE_MAP=y
    enable_crc32
    enable_sha256
}

enable_ntp() {
    USE_NTP=y
    enable_sock
    enable_xtime
}

enable_event() {
    USE_EVENT=y
    enable_sock
    enable_hash
}

enable_addr() {
    USE_ADDR=y
    enable_array
    enable_sock
    enable_xstr
}

enable_xjson() {
    USE_XJSON=y
    enable_map
    enable_xstr
    enable_array
}

enable_crypt() {
    USE_CRYPT=y
    enable_aes
    enable_rsa
    enable_hmac
    enable_xbuf
    enable_xstr
    enable_crc32
    enable_base64
    enable_sha256
    enable_sha1
    enable_md5
}

enable_hmac() {
    USE_HMAC=y
    enable_base64
    enable_sha256
    enable_sha1
    enable_xstr
    enable_md5
}

enable_jwt() {
    USE_JWT=y
    enable_xstr
    enable_xjson
    enable_array
    enable_crypt
    enable_hmac
    enable_base64
}

enable_http() {
    USE_HTTP=y
    enable_addr
    enable_sock
    enable_map
    enable_xbuf
    enable_xstr
    enable_crypt
    enable_base64
}

enable_mdtp() {
    USE_MDTP=y
    enable_xbuf
    enable_xstr
    enable_xjson
}

enable_sock() {
    USE_SOCK=y
    enable_sync
    enable_xbuf
    enable_xstr
}

enable_api() {
    USE_API=y
    enable_base64
    enable_event
    enable_xbuf
    enable_sha1
    enable_sock
    enable_http
    enable_mdtp
    enable_ws
}

enable_xcli() {
    USE_XCLI=y
    enable_xstr
    enable_list
    enable_xbuf
    enable_xtime
}

enable_xcpu() {
    USE_XCPU=y
    enable_sync
    enable_xstr
    enable_xfs
}

enable_xfs() {
    USE_XFS=y
    enable_array
    enable_xbuf
    enable_xstr
    enable_sync
}

enable_xlog() {
    USE_XLOG=y
    enable_xtime
    enable_xstr
    enable_sync
}

enable_xtop() {
    USE_XTOP=y
    enable_thread
    enable_xtype
    enable_array
    enable_sync
    enable_addr
    enable_xstr
    enable_xfs
}

modules=(
    "crypt: AES"
    "crypt: BASE64"
    "crypt: CRC32"
    "crypt: CRYPT"
    "crypt: HMAC"
    "crypt: MD5"
    "crypt: RSA"
    "crypt: SHA256"
    "crypt: SHA1"
    "data: ARRAY"
    "data: HASH"
    "data: JWT"
    "data: LIST"
    "data: MAP"
    "data: XBUF"
    "data: XJSON"
    "data: XSTR"
    "net: ADDR"
    "net: EVENT"
    "net: HTTP"
    "net: MDTP"
    "net: NTP"
    "net: RTP"
    "net: SOCK"
    "net: API"
    "net: WS"
    "sys: SYNC"
    "sys: THREAD"
    "sys: XCLI"
    "sys: XCPU"
    "sys: XFS"
    "sys: XLOG"
    "sys: XSIG"
    "sys: XTIME"
    "sys: XTOP"
    "sys: XTYPE"
)

# Fix dependencies
for mod in "${modules[@]}"; do
    IFS=': ' read -ra parts <<< "$mod"
    type=${parts[0]}
    name=${parts[1]}
    var="USE_$name"

    if [ -v $var ] && [ ${!var} == "y" ]; then
        func="enable_${name,,}"
        eval "$func"
    fi
done

if [[ $1 == "cmake" ]]; then

    extension='c'
    sourceList="${SOURCE_DIR}/xver.${extension}"

    for mod in "${modules[@]}"; do
        IFS=': ' read -ra parts <<< "$mod"
        dir=${parts[0]}
        name=${parts[1]}
        var="USE_$name"

        if [ -v $var ] && [ ${!var} == "y" ]; then
            echo "-- Using ${mod,,}.c"
            sourceList="${sourceList}\n    ${SOURCE_DIR}/${dir}/${name,,}.${extension}"
        fi
    done

    echo "-- Generating new CMakeLists.txt file..."
    cat ./misc/CMakeLists.tmp | sed -e "s#_SOURCE_LIST_#${sourceList}#" > ./CMakeLists.txt

elif [[ $1 == "make" ]]; then

    extension='$(OBJ) \\'
    sourceList="xver.${extension}"

    for mod in "${modules[@]}"; do
        IFS=': ' read -ra parts <<< "$mod"
        dir=${parts[0]}
        name=${parts[1]}
        var="USE_$name"

        if [ -v $var ] && [ ${!var} == "y" ]; then
            echo "-- Using ${mod,,}.c"
            sourceList="${sourceList}\n   ${name,,}.${extension}"
        fi
    done

    echo "-- Generating new Makefile..."
    cat ./misc/Makefile.tmp | sed -e "s#_SOURCE_LIST_#${sourceList}#" > ./misc/Makefile.1
    cat ./misc/Makefile.1 | sed -e "s#_SSL_FLAGS_#${sslFlags}#" > ./misc/Makefile.2
    cat ./misc/Makefile.2 | sed -e "s#_SSL_LIBS_#${sslLibs}#" > ./Makefile

    [ -f ./misc/Makefile.1 ] && rm -rf ./misc/Makefile.1
    [ -f ./misc/Makefile.2 ] && rm -rf ./misc/Makefile.2
fi
