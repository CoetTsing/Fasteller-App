本实验初步搭建了一个mlx网卡到redis数据库的流程

下一阶段的目标是在新的分支重构代码；测速；多线程聚合算法

————————————————————————————————

dpdk-pktgen发包：

cd ~/oldz/pktgen-dpdk-pktgen-20.11.1/build/app

sudo ./pktgen -l 0-15 -n 8 -- -P -m "[1 : 2-15].0" -s 0:/root/oldz/pcap/test.clean.pcap

-l：lcore数量

-n：内存通道数

-P：启动全部网口

-m：map映射core和网口

-s：source制定pcap文件

set 0 count 10000  # 设置数量

start 0

————————————————————————————————

testpmd测试：

cd dpdk_test_area/dpdk-stable/build/app/

sudo ./dpdk-testpmd -l 16-19 -n 4 -- -i --portmask=0x1

start

show port stats all

————————————————————————————————

编译应用程序：

cd dpdk/<build_dir>

meson configure -Dexamples=helloworld

ninja

________________________________

监控redis：

redis-cli --stat

关闭：

shutdown

exit

————————————————————————————————

运行聚合程序

sudo ./basicfwd -l 0-7 -n 8