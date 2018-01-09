LD_LIBRARY_PATH=$(pwd)/depends/build/lib ./configure \
    --enable-debug \
    --with-libraries=$(pwd)/depends/build/lib \
    --with-includes=$(pwd)/depends/build/include \
    --with-libxml --with-gssapi --prefix=/usr/local/gpdb/gpdb-$(date '+%Y-%m-%d')
#$(pwd)/build/gpdb-$(date '+%Y-%m-%d %H:%M:%S')