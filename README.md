To download and extract the ARM toolchain...

## Easier method for installing the GCC ARM toolchain, as it sets up the paths for you.
## It works fine for axi_test, but I'm not sure if the latest GCC ARM version may cause issues when compiling Main_MiSTer. (ElectronAsh).

sudo apt-get update
sudo apt-get install libc6-armel-cross libc6-dev-armel-cross binutils-arm-linux-gnueabi libncurses5-dev build-essential bison flex libssl-dev bc
sudo apt-get install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf


## This is the older method.
## Please skip this for now if the above method works for you.

wget -c https://releases.linaro.org/components/toolchain/binaries/6.5-2018.12/arm-linux-gnueabihf/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz
tar xf gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz -C /opt
export CC='/opt/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc'
export PATH=$PATH:/opt/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf/bin
export PATH=$PATH:/opt/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf/lib


## To compile axi_test...

arm-linux-gnueabihf-gcc axi_test.c -o axi_test


## To copy axi_test to the MiSTer SD card via ssh/scp...

sshpass -p "1" scp axi_test root@192.168.0.76:/media/fat/
sshpass -p '1' ssh root@192.168.0.76

(or whatever the IP address of your MiSTer is.)


## To run axi_test from the SD card root folder...

cd /media/fat
./axi_test


The core MUST have AXI bridge support enabled, else running axi_test will likely crash the HPS, and it will self-restart after 10-20 seconds.
