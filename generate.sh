#!/bin/bash

[ ! -f "CMakeLists.tmp" ] && \
    echo "CMakeLists template file is not found" && exit 1

. $(dirname "$0")/xutils.conf

enable_addr() {
    USE_ADDR=y
    USE_ARRAY=y
    enable_xstr
    enable_sock
}

enable_crypt() {
    USE_CRYPT=y
    USE_XAES=y
    enable_xstr
    enable_xbuf
}

enable_event() {
    USE_EVENT=y
    enable_hash
}

enable_hash() {
    USE_HASH=y
    USE_LIST=y
}

enable_http() {
    USE_HTTP=y
    enable_crypt
    enable_xstr
    enable_addr
    enable_xmap
    enable_sock
    enable_xbuf
}

enable_sock() {
    USE_SOCK=y
    USE_SYNC=y
    enable_xbuf
    enable_xstr
}

enable_thread() {
    USE_THREAD=y
    USE_SYNC=y
}

enable_xbuf() {
    USE_XBUF=y
    enable_xstr
}

enable_xcli() {
    USE_LIST=y
    enable_xtime
    enable_xbuf
}

enable_xcpu() {
    USE_XCPU=y
    USE_SYNC=y
    enable_xstr
    enable_xfs
}

enable_xfs() {
    USE_XFS=y
    USE_SYNC=y
    USE_ARRAY=y
    enable_xstr
    enable_xbuf
}

enable_xjson() {
    USE_XJSON=y
    USE_ARRAY=y
    enable_xmap
    enable_xstr
}

enable_xlog() {
    USE_XLOG=y
    USE_SYNC=y
    enable_xstr
    enable_xtime
}

enable_xmap() {
    USE_XMAP=y
    enable_crypt
}

enable_xstr() {
    USE_XSTR=y
    USE_ARRAY=y
    USE_XTYPE=y
    USE_SYNC=y
}

enable_xtime() {
    USE_XTIME=y
    enable_xstr
}

enable_xtop() {
    USE_ARRAY=y
    USE_SYNC=y
    enable_thread
    enable_addr
    enable_xstr
    enable_xfs
}

enable_xtype() {
    USE_XTYPE=y
    enable_xstr
}

enable_ntp() {
    USE_NTP=y
    enable_xtime
    enable_sock
}

enable_mdtp() {
    USE_MDTP=y
    enable_xbuf
    enable_xstr
    enable_xjson
}

enable_xapi() {
    USE_XAPI=y
    enable_xbuf
    enable_sock
    enable_http
    enable_event
}

# Fix dependencies
[ -v USE_ADDR ] && [ ${USE_ADDR} == "y" ] && enable_addr
[ -v USE_CRYPT ] && [ ${USE_CRYPT} == "y" ] && enable_crypt
[ -v USE_EVENT ] && [ ${USE_EVENT} == "y" ] && enable_event
[ -v USE_HASH ] && [ ${USE_HASH} == "y" ] && enable_hash
[ -v USE_HTTP ] && [ ${USE_HTTP} == "y" ] && enable_http
[ -v USE_SOCK ] && [ ${USE_SOCK} == "y" ] && enable_sock
[ -v USE_THREAD ] && [ ${USE_THREAD} == "y" ] && enable_thread
[ -v USE_XBUF ] && [ ${USE_XBUF} == "y" ] && enable_xbuf
[ -v USE_XCLI ] && [ ${USE_XCLI} == "y" ] && enable_xcli
[ -v USE_XCPU ] && [ ${USE_XCPU} == "y" ] && enable_xcpu
[ -v USE_XFS ] && [ ${USE_XFS} == "y" ] && enable_xfs
[ -v USE_XJSON ] && [ ${USE_XJSON} == "y" ] && enable_xjson
[ -v USE_XLOG ] && [ ${USE_XLOG} == "y" ] && enable_xlog
[ -v USE_XMAP ] && [ ${USE_XMAP} == "y" ] && enable_xmap
[ -v USE_XSTR ] && [ ${USE_XSTR} == "y" ] && enable_xstr
[ -v USE_XTIME ] && [ ${USE_XTIME} == "y" ] && enable_xtime
[ -v USE_XTOP ] && [ ${USE_XTOP} == "y" ] && enable_xtop
[ -v USE_XTYPE ] && [ ${USE_XTYPE} == "y" ] && enable_xtype
[ -v USE_NTP ] && [ ${USE_NTP} == "y" ] && enable_ntp
[ -v USE_MDTP ] && [ ${USE_MDTP} == "y" ] && enable_mdtp
[ -v USE_XAPI ] && [ ${USE_XAPI} == "y" ] && enable_xapi

# Enable particular functionality
sourceList="\${SOURCE_DIR}\/xver.c"

[ -v USE_ADDR ] && [ ${USE_ADDR} == "y" ] && \
    echo "-- Using addr.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/addr.c"

[ -v USE_ARRAY ] && [ ${USE_ARRAY} == "y" ] && \
    echo "-- Using array.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/array.c"

[ -v USE_CRYPT ] && [ ${USE_CRYPT} == "y" ] && \
    echo "-- Using crypt.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/crypt.c"

[ -v USE_ERREX ] && [ ${USE_ERREX} == "y" ] && \
    echo "-- Using errex.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/errex.c"

[ -v USE_EVENT ] && [ ${USE_EVENT} == "y" ] && \
    echo "-- Using event.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/event.c"

[ -v USE_HASH ] && [ ${USE_HASH} == "y" ] && \
    echo "-- Using hash.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/hash.c"

[ -v USE_HTTP ] && [ ${USE_HTTP} == "y" ] && \
    echo "-- Using http.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/http.c"

[ -v USE_LIST ] && [ ${USE_LIST} == "y" ] && \
    echo "-- Using list.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/list.c"

[ -v USE_SOCK ] && [ ${USE_SOCK} == "y" ] && \
    echo "-- Using sock.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/sock.c"

[ -v USE_SYNC ] && [ ${USE_SYNC} == "y" ] && \
    echo "-- Using sync.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/sync.c"

[ -v USE_THREAD ] && [ ${USE_THREAD} == "y" ] && \
    echo "-- Using thread.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/thread.c"

[ -v USE_XAES ] && [ ${USE_XAES} == "y" ] && \
    echo "-- Using xaes.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xaes.c"

[ -v USE_XBUF ] && [ ${USE_XBUF} == "y" ] && \
    echo "-- Using xbuf.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xbuf.c"

[ -v USE_XCLI ] && [ ${USE_XCLI} == "y" ] && \
    echo "-- Using xcli.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xcli.c"

[ -v USE_XCPU ] && [ ${USE_XCPU} == "y" ] && \
    echo "-- Using xcpu.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xcpu.c"

[ -v USE_XFS ] && [ ${USE_XFS} == "y" ] && \
    echo "-- Using xfs.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xfs.c"

[ -v USE_XJSON ] && [ ${USE_XJSON} == "y" ] && \
    echo "-- Using xjson.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xjson.c"

[ -v USE_XLOG ] && [ ${USE_XLOG} == "y" ] && \
    echo "-- Using xlog.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xlog.c"

[ -v USE_XMAP ] && [ ${USE_XMAP} == "y" ] && \
    echo "-- Using xmap.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xmap.c"

[ -v USE_XSTR ] && [ ${USE_XSTR} == "y" ] && \
    echo "-- Using xstr.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xstr.c"

[ -v USE_XTIME ] && [ ${USE_XTIME} == "y" ] && \
    echo "-- Using xtime.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xtime.c"

[ -v USE_XTOP ] && [ ${USE_XTOP} == "y" ] && \
    echo "-- Using xtop.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xtop.c"

[ -v USE_XTYPE ] && [ ${USE_XTYPE} == "y" ] && \
    echo "-- Using xtype.c" && \
    sourceList="${sourceList}\n    \${SOURCE_DIR}\/xtype.c"

[ -v USE_XAPI ] && [ ${USE_XAPI} == "y" ] && \
    echo "-- Using xapi.c" && \
    sourceList="${sourceList}\n    \${MEDIA_DIR}\/xapi.c"

[ -v USE_MDTP ] && [ ${USE_MDTP} == "y" ] && \
    echo "-- Using mdtp.c" && \
    sourceList="${sourceList}\n    \${MEDIA_DIR}\/mdtp.c"

[ -v USE_NTP ] && [ ${USE_NTP} == "y" ] && \
    echo "-- Using ntp.c" && \
    sourceList="${sourceList}\n    \${MEDIA_DIR}\/ntp.c"

[ -v USE_RTP ] && [ ${USE_RTP} == "y" ] && \
    echo "-- Using rtp.c" && \
    sourceList="${sourceList}\n    \${MEDIA_DIR}\/rtp.c"

[ -v USE_TS ] && [ ${USE_TS} == "y" ] && \
    echo "-- Using ts.c" && \
    sourceList="${sourceList}\n    \${MEDIA_DIR}\/ts.c"

echo "-- Generating new CMakeLists.txt file..."
cat CMakeLists.tmp | sed -e "s/_SOURCE_LIST_/${sourceList}/" > CMakeLists.txt