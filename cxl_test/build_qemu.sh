wget https://download.qemu.org/qemu-8.2.0.tar.xz
tar -xvf qemu-8.2.0.tar.xz
cd qemu-8.2.0
mkdir -p build && cd build
../configure -target-list=i386-softmmu,x86_64-softmmu --enable-libpmem --enable-slirp
make -j all
