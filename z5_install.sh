DEST_NAME=gpdb-$(date '+%Y-%m-%d')
pushd /usr/local/gpdb && \
chown -R gpadmin $DEST_NAME
tar -cvf $DEST_NAME.tar ./$DEST_NAME && \
popd && \
ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db && \
su -l -c "gpscp -f /home/gpadmin/gpconfigs/hostfile /usr/local/gpdb/$DEST_NAME.tar =:/usr/local/gpdb/$DEST_NAME.tar" gpadmin && \
su -l -c "gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'tar xvC /usr/local/gpdb -f /usr/local/gpdb/$DEST_NAME.tar'" gpadmin && \
su -l -c "gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db'" gpadmin && \
su -l -c "gpscp -f /home/gpadmin/gpconfigs/hostfile /usr/local/greenplum-db/lib/postgresql/hyperloglog_counter.so =:/usr/local/greenplum-db/lib/postgresql/" gpadmin
#exit
