#DEST_NAME=gpdb-$(date '+%Y-%m-%d')
DEST_NAME=`cat dest_name`
if [ -z "$DEST_NAME" ]; then
  echo "DEST_NAME is unset or set to the empty string"
  exit 1
fi

#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'printf "\nsource /usr/local/greenplum-db/greenplum_path.sh\n" >> /home/gpadmin/.bashrc'
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'echo "9wdJtda8WAYJ" | passwd gpadmin --stdin'
#exit


source /usr/local/gpdb/$DEST_NAME/greenplum_path.sh
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'mkdir -p /data_5/d3/mirror/'
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'mkdir -p /data_5/d4/mirror/'
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'rm -rf /data'
gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'chown -R gpadmin /usr/local/'
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'chown -R gpadmin /data_5'
gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'chown -R gpadmin /home/gpadmin'
gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'mkdir -p /usr/local/gpdb'
gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'chown gpadmin /usr/local/gpdb'



#exit

pushd /usr/local/gpdb && \
chown -R gpadmin $DEST_NAME
tar -cvf $DEST_NAME.tar ./$DEST_NAME && \
chown -R gpadmin /usr/local/gpdb && \
popd && \
ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db && \
su -l -c "gpscp -f /home/gpadmin/gpconfigs/hostfile /usr/local/gpdb/$DEST_NAME.tar =:/usr/local/gpdb/$DEST_NAME.tar" gpadmin && \
su -l -c "gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'tar xvC /usr/local/gpdb -f /usr/local/gpdb/$DEST_NAME.tar'" gpadmin && \
su -l -c "gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'ln -f -s -T /usr/local/gpdb/$DEST_NAME /usr/local/greenplum-db'" gpadmin && \
su -l -c "gpscp -f /home/gpadmin/gpconfigs/hostfile /usr/local/greenplum-db/lib/postgresql/hyperloglog_counter.so =:/usr/local/greenplum-db/lib/postgresql/" gpadmin
#exit
