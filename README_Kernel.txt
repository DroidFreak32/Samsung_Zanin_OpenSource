################################################################################
HOW TO BUILD KERNEL FOR GT-B5330_EUR

1. How to Build
	- get Toolchain
	download and install arm-eabi-4.4.3 toolchain for ARM EABI.
	Extract kernel source and move into the top directory.

	$ export CROSS_COMPILE=/opt/toolchains/arm-eabi-4.4.3/bin/arm-eabi-
	$ make bcm21654_rhea_ss_zanin_rev05_defconfig 
	$ make
	
2. Output files
	- Kernel : Kernel/arch/arm/boot/zImage
	- module : Kernel/drivers/*/*.ko
	
3. How to Clean	
    $ make clean
	
4. How to make .tar binary for downloading into target.
	- change current directory to Kernel/arch/arm/boot
	- type following command
	$ tar cvf GT-B5330_EUR.tar zImage
################################################################################