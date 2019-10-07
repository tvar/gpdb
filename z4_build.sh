DEST_NAME=`cat dest_name`
if [ -z "$DEST_NAME" ]; then
  echo "DEST_NAME is unset or set to the empty string"
  exit 1
fi
#gpdb-$(date '+%Y-%m-%d')
mkdir /usr/local/gpdb/$DEST_NAME

#: <<'COMMENT'
make && \
make install && \
cp -R ./depends/build/* /usr/local/gpdb/$DEST_NAME && \
chown -R gpadmin /usr/local/gpdb/$DEST_NAME &&\
 \
source /usr/local/gpdb/$DEST_NAME/greenplum_path.sh && \
pushd /home/gpadmin/postgres_hyperloglog && \
make install && \
popd
#ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db && \
#export MASTER_DATA_DIRECTORY=/data-5.0/master/gpseg-1
#su -l -c "make install" gpadmin && \