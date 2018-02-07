pushd depends
./configure --prefix=$(pwd)/build_temp
make install_local
popd