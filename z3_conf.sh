DEST_NAME=gpdb-$(date '+%Y-%m-%d')
i="0"
while [ -d "/usr/local/gpdb/$DEST_NAME" ]
do
  i=$[$i+1]
  DEST_NAME=gpdb-$(date '+%Y-%m-%d').$i
done
echo "$DEST_NAME" > dest_name
echo `cat dest_name`

LD_LIBRARY_PATH=$(pwd)/depends/build/lib ./configure \
    --enable-debug \
    --with-zstd \
    --with-libraries=$(pwd)/depends/build/lib \
    --with-includes=$(pwd)/depends/build/include \
    --with-libxml --with-gssapi --prefix=/usr/local/gpdb/$DEST_NAME

#yum install libzstd-devel libzstd