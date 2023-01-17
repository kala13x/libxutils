export XUTILS_USE_SSL=y

cd .. && ./build.sh smake
sudo make install
cd examples

if [ ! -z "$1" ]; then
    FILE=${1/%.c/}
    make clean
    make $FILE

    valgrind --leak-check=full \
        --show-leak-kinds=all \
        --show-error-list=yes \
        --track-origins=yes \
        ./$FILE $2 $3 $4 $5 $6 $7 $8 $9
else
    make clean
    make
fi
