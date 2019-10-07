#DEST_NAME=gpdb-$(date '+%Y-%m-%d')
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'printf "\nsource /usr/local/greenplum-db/greenplum_path.sh\n" >> /home/gpadmin/.bashrc'
#gpssh -f /home/gpadmin/gpconfigs/hostfile -v -e 'echo "9wdJtda8WAYJ" | passwd gpadmin --stdin'
#exit
#source /usr/local/gpdb/$DEST_NAME/greenplum_path.sh

su -l -s /bin/bash -c "rm -rf ~/go/src/github.com/greenplum-db/gpbackup && \
  rm -rf ~/go/pkg/dep/sources/* && \
  go get github.com/greenplum-db/gp-common-go-libs/... &&\
  go get -u github.com/greenplum-db/gpbackup/... && \
  echo "-------------------------" &&\
  pushd ~/go/src/github.com/greenplum-db/gpbackup &&\
  git remote add tvar https://github.com/tvar/gpbackup &&\
  git pull tvar master &&\
  make depend && \
  make build && \
  cp -f ~/go/bin/gpbackup  /usr/local/greenplum-db/bin/ && \
  cp -f ~/go/bin/gprestore  /usr/local/greenplum-db/bin/" gpadmin
  
#  make depend && \
