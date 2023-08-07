#!/bin/bash

. $(dirname "$0")/../xutils.conf
SOURCE_DIR=./src

sourceList=""
extension=""
sslFlags=""
sslLibs=""

if [[ $1 == "make" ]]; then

    [ ! -f "./Makefile.tmp" ] && \
        echo "Makefile template file is not found" && exit 1

    extension='$(OBJ) \\'
    sourceList="xver.${extension}"

elif [[ $1 == "cmake" ]]; then

    [ ! -f "./CMakeLists.tmp" ] && \
        echo "CMakeLists template file is not found" && exit 1

    extension="c"
    sourceList="${SOURCE_DIR}/xver.${extension}"

elif [[ $1 == "smake" ]]; then

    [ ! -f "./smake.tmp" ] && \
        echo "SMake template file is not found" && exit 1

    extension="c"
    sourceList="\"${SOURCE_DIR}/xver.${extension}\","

else
    echo "Invalid make tool.";
    exit 1;
fi

for arg in "$@"; do
    if [[ $arg == "--ssl" ]]; then
        sslFlags="-D_XUTILS_USE_SSL"
        sslLibs="-lssl -lcrypto"
    fi
done

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

for mod in "${modules[@]}"; do
    IFS=': ' read -ra parts <<< "$mod"
    dir=${parts[0]}
    name=${parts[1]}
    var="USE_$name"

    if [ -v $var ] && [ ${!var} == "y" ]; then
        echo "-- Using ${mod,,}.c"
        if [[ $1 == "make" ]]; then
            sourceList="${sourceList}\n   ${name,,}.${extension}"
        elif [[ $1 == "cmake" ]]; then
            sourceList="${sourceList}\n    ${SOURCE_DIR}/${dir}/${name,,}.${extension}"
        elif [[ $1 == "smake" ]]; then
            sourceList="${sourceList}\n            \"${SOURCE_DIR}/${dir}/${name,,}.${extension}\","
        fi
    fi
done

if [[ "${sourceList: -1}" == "," ]]; then
    sourceList="${sourceList%,}"
fi

if [[ $1 == "cmake" ]]; then
    echo "-- Generating new CMakeLists.txt file..."
    cat ./CMakeLists.tmp | sed -e "s#_SOURCE_LIST_#${sourceList}#" > ../CMakeLists.txt
elif [[ $1 == "smake" ]]; then
    echo "-- Generating new smake.json file..."
    cat ./smake.tmp | sed -e "s#_SOURCE_LIST_#${sourceList}#" > ../smake.json
elif [[ $1 == "make" ]]; then
    echo "-- Generating new Makefile..."
    cat ./Makefile.tmp | sed -e "s#_SOURCE_LIST_#${sourceList}#" > ./Makefile.1
    cat ./Makefile.1 | sed -e "s#_SSL_FLAGS_#${sslFlags}#" > ./Makefile.2
    cat ./Makefile.2 | sed -e "s#_SSL_LIBS_#${sslLibs}#" > ../Makefile

    [ -f ./Makefile.1 ] && rm -rf ./Makefile.1
    [ -f ./Makefile.2 ] && rm -rf ./Makefile.2
fi
