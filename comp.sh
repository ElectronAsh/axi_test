arm-linux-gnueabihf-gcc axi_test.c -o axi_test
sshpass -p "1" scp axi_test root@192.168.0.76:/media/fat/
sshpass -p '1' ssh root@192.168.0.76
