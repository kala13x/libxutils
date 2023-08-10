#!/bin/bash

. $(dirname "$0")/../xutils.conf
SOURCE_DIR=./src

installPrefix="/usr/local"
sourceList=""
extension=""
sslObject=""
sslFlags=""
sslLibs=""
useSSL="yes"

SMAKE_SSL_OBJ="\"find\": {\n\
            \"libssl.so:libcrypto.so\": {\n\
                \"path\": \"/usr/local/ssl/lib:/usr/local/ssl/lib64\",\n\
                \"flags\": \"-D_XUTILS_USE_SSL\",\n\
                \"libs\": \"-lssl -lcrypto\"\n\
            }\n\
        }"

CMAKE_SSL_OBJ="find_package(OpenSSL)\n\
IF(OpenSSL_FOUND)\n\
set(CMAKE_C_FLAGS \"\${CMAKE_C_FLAGS} -D_XUTILS_USE_SSL\")\n\
ENDIF()"

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

validate_args() {
    case $1 in
        "make")
            [ ! -f "./Makefile.tmp" ] && echo "Makefile template file is not found" && exit 1
            extension='$(OBJ) \\'
            sourceList="xver.${extension}"
            ;;
        "cmake")
            [ ! -f "./CMakeLists.tmp" ] && echo "CMakeLists template file is not found" && exit 1
            extension="c"
            sourceList="${SOURCE_DIR}/xver.${extension}"
            ;;
        "smake")
            [ ! -f "./smake.tmp" ] && echo "SMake template file is not found" && exit 1
            extension="c"
            sourceList="\"${SOURCE_DIR}/xver.${extension}\","
            ;;
        *)
            echo "Invalid make tool."
            exit 1
            ;;
    esac
}

fix_dependencies() {
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
}

parse_cli_args() {
    for arg in "$@"; do

        if [[ $arg == --ssl=* ]]; then
            useSSL="${arg#*=}"
        fi

        if [[ $arg == --prefix=* ]]; then
            installPrefix="${arg#*=}"
        fi

    done

    if [[ "${installPrefix: -1}" == "/" ]]; then
        installPrefix="${installPrefix%/}"
    fi

    echo "-- Prefix: $installPrefix"
    echo "-- Using SSL: $useSSL"
}

process_modules() {
    for mod in "${modules[@]}"; do
        IFS=': ' read -ra parts <<< "$mod"
        dir=${parts[0]}
        name=${parts[1]}
        var="USE_$name"

        if [ -v $var ] && [ ${!var} == "y" ]; then
            echo "-- Using ${mod,,}.c"
            case $1 in
                "make") sourceList="${sourceList}\n   ${name,,}.${extension}" ;;
                "cmake") sourceList="${sourceList}\n    ${SOURCE_DIR}/${dir}/${name,,}.${extension}" ;;
                "smake") sourceList="${sourceList}\n            \"${SOURCE_DIR}/${dir}/${name,,}.${extension}\"," ;;
            esac
        fi
    done

    if [[ "${sourceList: -1}" == "," ]]; then
        sourceList="${sourceList%,}"
    fi
}

cleanup_temp_files() {
    [ -f ./CMakeLists.1 ] && rm -f ./CMakeLists.1
    [ -f ./CMakeLists.2 ] && rm -f ./CMakeLists.2
    [ -f ./Makefile.1 ] && rm -f ./Makefile.1
    [ -f ./Makefile.2 ] && rm -f ./Makefile.2
    [ -f ./smake.2 ] && rm -f ./smake.2
    [ -f ./smake.1 ] && rm -f ./smake.1
}

prepare_ssl_objects() {
    if [[ $useSSL == "yes" ]]; then
        case $1 in
            "cmake")
                sslObject=$CMAKE_SSL_OBJ
                ;;
            "smake")
                sslObject=$SMAKE_SSL_OBJ
                ;;
            "make")
                sslFlags="-D_XUTILS_USE_SSL"
                sslLibs="-lssl -lcrypto"
                ;;
        esac
    fi
}

generate_files() {
    case $1 in
        "cmake")
            echo "-- Generating new CMakeLists.txt file..."
            sed -e "s#_INSTALL_PREFIX_#${installPrefix}#" < ./CMakeLists.tmp > ./CMakeLists.1
            sed -e "s#_SOURCE_LIST_#${sourceList}#" < ./CMakeLists.1 > ./CMakeLists.2
            sed -e "s#_SSL_OBJECT_#${sslObject}#" < ./CMakeLists.2 > ../CMakeLists.txt
            ;;
        "smake")
            echo "-- Generating new smake.json file..."
            sed -e "s#_INSTALL_PREFIX_#${installPrefix}#" < ./smake.tmp > ./smake.1
            sed -e "s#_SOURCE_LIST_#${sourceList}#" < ./smake.1 > ./smake.2
            sed -e "s#_SSL_OBJECT_#${sslObject}#" < ./smake.2 > ../smake.json
            ;;
        "make")
            echo "-- Generating new Makefile..."
            sed -e "s#_INSTALL_PREFIX_#${installPrefix}#" < ./Makefile.tmp > ./Makefile.1
            sed -e "s#_SOURCE_LIST_#${sourceList}#" < ./Makefile.1 > ./Makefile.2
            sed -e "s#_SSL_FLAGS_#${sslFlags}#" < ./Makefile.2 > ./Makefile.1
            sed -e "s#_SSL_LIBS_#${sslLibs}#" < ./Makefile.1 > ../Makefile
            ;;
    esac
}

# Validate CLI args
validate_args "$1"
parse_cli_args "$@"

# Fix deps & enable modules
prepare_ssl_objects "$1"
fix_dependencies "$@"
process_modules "$1"

# Generate build files
generate_files "$1"
cleanup_temp_files