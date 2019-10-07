git clone https://github.com/greenplum-db/gp-xerces
mkdir gp-xerces/build
pushd gp-xerces/build
git pull
../configure --prefix=$(pwd)/../../depends/build
make clean
make install

../configure
make clean
make install



popd