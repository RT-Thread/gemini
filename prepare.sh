# download RT-Thread git code
# https://github.com/RT-Thread/rt-thread.git

git clone https://git.coding.net/RT-Thread/rt-thread.git

# install 32bit environment
sudo apt-get update
sudo apt-get -y install lib32z1 astyle
# prepare toolchains and scons 
sudo apt-get -y install scons qemu-system-arm libncurses5-dev zip bc texinfo

# clone package files repo
if [ ! -d "packages-files" ]; then
git clone https://git.coding.net/RT-Thread/packages-files.git
else
cd packages-files
git pull origin
cd ..
fi

if [ ! -d "arm-2012.09" ]; then
tar jxvf packages-files/arm-2012.09-63-arm-none-eabi-i686-pc-linux-gnu.tar.bz2
fi

if [ ! -d "linux-3.18.16" ]; then
tar Jxvf packages-files/linux-3.18.16.tar.xz
cd linux-3.18.16
patch -p1 < ../rtthread_vbus.patch
cd ..
fi

if [ ! -d "buildroot-2015.05" ]; then
tar jxvf packages-files/buildroot-2015.05.tar.bz2
mkdir -p buildroot-2015.05/dl
cp packages-files/dl/* buildroot-2015.05/dl
fi 

if [ ! -d "gcc-linaro-4.9-2014.11-x86_64_arm-linux-gnueabi" ]; then
tar Jxvf packages-files/gcc-linaro-4.9-2014.11-x86_64_arm-linux-gnueabi.tar.xz
fi

