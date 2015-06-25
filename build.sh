cd buildroot-2015.05
make 
cd ..

cd boot
./build.sh
cd ..

cd vexpress
./build.sh
cd ..

cd rtloader
./build.sh
cd ..

cd linux-apps
make 
cp vecho $MO_DIR
cd ..

cd buildroot-2015.05
make 
cd ..

