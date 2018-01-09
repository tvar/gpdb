DEST_NAME=gpdb-$(date '+%Y-%m-%d')
#: <<'COMMENT'
make && \
make install && \
cp -R ./depends/build/* /usr/local/gpdb/$DEST_NAME && \
 \
source /usr/local/gpdb/$DEST_NAME/greenplum_path.sh && \
pushd /home/gpadmin/postgres_hyperloglog && \
make install && \
popd
#ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db && \
#export MASTER_DATA_DIRECTORY=/data-5.0/master/gpseg-1
#su -l -c "make install" gpadmin && \