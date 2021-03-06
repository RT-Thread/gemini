# download RT-Thread git code
git clone https://github.com/RT-Thread/rt-thread.git
# install 32bit environment
sudo apt-get update
sudo apt-get update
sudo apt-get -y install lib32z1 astyle
# prepare toolchains and scons 
sudo apt-get -y install scons qemu-system-arm libncurses5-dev zip bc texinfo

# clone package files repo
git clone https://git.coding.net/RT-Thread/packages-files.git

# download toolchain
if [ ! -f "arm-2012.09-63-arm-none-eabi-i686-pc-linux-gnu.tar.bz2" ];then
wget -c https://sourcery.mentor.com/public/gnu_toolchain/arm-none-eabi/arm-2012.09-63-arm-none-eabi-i686-pc-linux-gnu.tar.bz2
tar jxvf arm-2012.09-63-arm-none-eabi-i686-pc-linux-gnu.tar.bz2
fi

# download Linux 3.18.x
if [ ! -f "linux-3.18.16.tar.xz" ]; then
wget -c https://www.kernel.org/pub/linux/kernel/v3.x/linux-3.18.16.tar.xz
tar Jxvf linux-3.18.16.tar.xz
cd linux-3.18.16
patch -p1 < ../rtthread_vbus.patch
cd ..
fi

# download buildroot
if [ ! -f "buildroot-2015.05.tar.bz2" ]; then
wget -c http://www.buildroot.org/downloads/buildroot-2015.05.tar.bz2
tar jxvf buildroot-2015.05.tar.bz2
fi 

if [ ! -f "gcc-linaro-4.9-2014.11-x86_64_arm-linux-gnueabi.tar.xz" ]; then
wget -c http://releases.linaro.org/14.11/components/toolchain/binaries/arm-linux-gnueabi/gcc-linaro-4.9-2014.11-x86_64_arm-linux-gnueabi.tar.xz
tar Jxvf gcc-linaro-4.9-2014.11-x86_64_arm-linux-gnueabi.tar.xz
fi
