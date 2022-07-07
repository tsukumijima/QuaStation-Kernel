# -----------------------------------------------------------------------
# ref: https://github.com/BPI-SINOVOIP/BPI-W2-bsp/blob/master/Makefile
# -----------------------------------------------------------------------

# directory
BASE_DIR := $(shell pwd)
LINUX_DIR := $(BASE_DIR)/linux

# cross compile prefix
CROSS_COMPILE := aarch64-linux-gnu-

# get processor count
J := $(shell expr `grep ^processor /proc/cpuinfo  | wc -l` \* 2)

# enable gcc colors
export GCC_COLORS='error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'

# create a directory for your build to reduce the size of your build context
docker-image:
	@echo '--------------------------------------------------------------------------------'
	@echo 'Building Docker image...'
	@echo '--------------------------------------------------------------------------------'
	mkdir -p build
	docker build -t quastation-kernel -f Dockerfile build/
	rmdir build
	@echo '--------------------------------------------------------------------------------'
	@echo 'Docker image build is completed.'
	@echo '--------------------------------------------------------------------------------'

build:
	docker run --rm -i -t -v `pwd`:/build/ quastation-kernel /bin/bash -c 'make build-in-container'

build-in-container:
	@echo '--------------------------------------------------------------------------------'
	@echo 'Building the kernel...'
	@echo '--------------------------------------------------------------------------------'
	cp $(LINUX_DIR)/arch/arm64/configs/rtd1295_quastation_defconfig $(LINUX_DIR)/.config
	$(Q)$(MAKE) -C linux/ ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$J INSTALL_MOD_PATH=output/ Image dtbs
	$(Q)$(MAKE) -C linux/ ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$J INSTALL_MOD_PATH=output/ modules
	@echo '--------------------------------------------------------------------------------'
	@echo 'Installing kernel modules...'
	@echo '--------------------------------------------------------------------------------'
	$(Q)$(MAKE) -C linux/ ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$J INSTALL_MOD_PATH=output/ modules_install
	@echo '--------------------------------------------------------------------------------'
	@echo 'Building phoenix drivers...'
	@echo '--------------------------------------------------------------------------------'
	mkdir -p $(LINUX_DIR)/output/lib/modules/4.1.17-quastation/kernel/extra/
	$(Q)$(MAKE) -C phoenix/drivers ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) TARGET_KDIR=$(LINUX_DIR) -j$J INSTALL_MOD_PATH=output/
	@echo '--------------------------------------------------------------------------------'
	@echo 'Installing phoenix drivers...'
	@echo '--------------------------------------------------------------------------------'
	$(Q)$(MAKE) -C phoenix/drivers ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) TARGET_KDIR=$(LINUX_DIR) -j$J INSTALL_MOD_PATH=output/ install
	@echo '--------------------------------------------------------------------------------'
	@echo 'Installing kernel headers...'
	@echo '--------------------------------------------------------------------------------'
	$(BASE_DIR)/install_kernel_headers.sh $(CROSS_COMPILE)
	@echo '--------------------------------------------------------------------------------'
	@echo 'Installing prebuilt binaries...'
	@echo '--------------------------------------------------------------------------------'
	cp -a $(BASE_DIR)/prebuilt/rtlbt/ $(LINUX_DIR)/output/lib/firmware/
	cp -a $(BASE_DIR)/prebuilt/openmax/* $(LINUX_DIR)/output/
	rm $(LINUX_DIR)/output/lib/modules/4.1.17-quastation/kernel/extra/mali_kbase.ko  # because currently doesn't work
	depmod --all --basedir=$(LINUX_DIR)/output/ 4.1.17-quastation
	@echo '--------------------------------------------------------------------------------'
	@echo 'Creating a package for USB flash...'
	@echo '--------------------------------------------------------------------------------'
	rm -rf $(BASE_DIR)/usbflash/
	mkdir -p $(BASE_DIR)/usbflash/
	mkdir -p $(BASE_DIR)/usbflash/bootfs/
	cp -a $(LINUX_DIR)/arch/arm64/boot/Image $(BASE_DIR)/usbflash/bootfs/uImage
	cp -a $(LINUX_DIR)/arch/arm64/boot/dts/realtek/rtd-1295-quastation.dtb $(BASE_DIR)/usbflash/bootfs/QuaStation.dtb
	mkdir -p $(BASE_DIR)/usbflash/rootfs/
	cp -a $(LINUX_DIR)/output/* $(BASE_DIR)/usbflash/rootfs/
	chown -R root:root usbflash/
	@echo '--------------------------------------------------------------------------------'
	@echo 'Kernel build is completed.'
	@echo '--------------------------------------------------------------------------------'

config:
	docker run --rm -i -t -v `pwd`:/build/ quastation-kernel /bin/bash -c 'make config-in-container'

config-in-container:
	cp $(LINUX_DIR)/arch/arm64/configs/rtd1295_quastation_defconfig $(LINUX_DIR)/.config
	$(Q)$(MAKE) -C linux/ ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$J menuconfig
	cp $(LINUX_DIR)/.config $(LINUX_DIR)/arch/arm64/configs/rtd1295_quastation_defconfig

clean:
	docker run --rm -i -t -v `pwd`:/build/ quastation-kernel /bin/bash -c 'make clean-in-container'

clean-in-container:
	$(Q)$(MAKE) -C phoenix/drivers ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) TARGET_KDIR=$(LINUX_DIR) -j$J INSTALL_MOD_PATH=output/ clean
	$(Q)$(MAKE) -C linux/ ARCH=arm64 CROSS_COMPILE=$(CROSS_COMPILE) -j$J distclean
	rm -rf $(LINUX_DIR)/output/
