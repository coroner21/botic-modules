# DESTDIR is used to install into a different root directory
DESTDIR?=/
# Specify the kernel directory to use
KERNELDIR?=/lib/modules/$(shell uname -r)/build
# Need the absolute directory do the driver directory to build kernel modules
CARDDIR?=$(shell pwd)/card
CODECSDIR?=$(shell pwd)/codecs
MCASPDIR?=$(shell pwd)/davinci-mcasp
DTSDIR?=$(shell pwd)/dts
#
# Where kernel drivers are going to be installed
MODULEDIR?=/lib/modules/$(shell uname -r)/extramodules

# Where updated kernel drivers are going to be installed
UPDATEDIR?=/lib/modules/$(shell uname -r )/updates

# Modules compilation
modules:
	@echo -e "\n::\033[32m Compiling Botic kernel modules\033[0m"
	@echo "========================================"
	$(MAKE) -C $(KERNELDIR) M="$(CARDDIR)" modules
	$(MAKE) -C $(KERNELDIR) M="$(CODECSDIR)" modules
	$(MAKE) -C $(KERNELDIR) M="$(MCASPDIR)" modules

modules_clean:
	@echo -e "\n::\033[32m Cleaning Botic kernel modules\033[0m"
	@echo "========================================"
	$(MAKE) -C "$(KERNELDIR)" M="$(CARDDIR)" clean
	$(MAKE) -C "$(KERNELDIR)" M="$(CODECSDIR)" clean
	$(MAKE) -C "$(KERNELDIR)" M="$(MCASPDIR)" clean

modules_install:
	@echo -e "\n::\033[34m Installing Botic kernel modules\033[0m"
	@echo "====================================================="
	@cp -v $(CARDDIR)/*.ko $(DESTDIR)/$(MODULEDIR)
	@cp -v $(CODECSDIR)/*.ko $(DESTDIR)/$(MODULEDIR)
	@cp -v $(MCASPDIR)/*.ko $(DESTDIR)/$(UPDATEDIR)
	@chown -v root:root $(DESTDIR)/$(MODULEDIR)/*.ko
	@chown -v root:root $(DESTDIR)/$(UPDATEDIR)/*.ko
	depmod

# Just use for packaging botic modules, not for installing manually
modules_install_packaging:
	@echo -e "\n::\033[34m Installing Botic kernel modules\033[0m"
	@echo "====================================================="
	@cp -v $(CARDDIR)/*.ko $(DESTDIR)/$(MODULEDIR)
	@cp -v $(CODECSDIR)/*.ko $(DESTDIR)/$(MODULEDIR)
	@cp -v $(MCASPDIR)/*.ko $(DESTDIR)/$(UPDATEDIR)

modules_uninstall:
	@echo -e "\n::\033[34m Uninstalling Botic kernel modules\033[0m"
	@echo "====================================================="
	@rm -fv $(DESTDIR)/$(MODULEDIR)/snd-soc-botic-codec.ko
	@rm -fv $(DESTDIR)/$(MODULEDIR)/snd-soc-botic.ko
	@rm -fv $(DESTDIR)/$(MODULEDIR)/snd-soc-es9018k2m.ko
	@rm -fv $(DESTDIR)/$(MODULEDIR)/snd-soc-sabre32.ko
	@rm -fv $(DESTDIR)/$(UPDATEDIR)/snd-soc-davinci-mcasp.ko

setup_dkms:
	@echo -e "\n::\033[34m Installing DKMS files\033[0m"
	@echo "====================================================="
	install -m 644 -v -D Makefile $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/Makefile
	install -m 644 -v -D dkms/dkms.conf $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/dkms.conf
	install -m 755 -v -d card $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/card
	install -m 755 -v -d codecs $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/codecs
	install -m 755 -v -d davinci-mcasp $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/davinci-mcasp
	install -m 644 -v card/* $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/card/
	install -m 644 -v codecs/* $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/codecs/
	install -m 644 -v davinci-mcasp/* $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)/davinci-mcasp/

remove_dkms:
	@echo -e "\n::\033[34m Removing DKMS files\033[0m"
	@echo "====================================================="
	rm -rf $(DESTDIR)/usr/src/$(DKMS_NAME)-$(DKMS_VER)

dtbs:
	@echo -e "\n::\033[34m Generating device tree overlay files\033[0m"
	@echo "====================================================="
	$(MAKE) -C $(DTSDIR) all

dtbs_clean:
	@echo -e "\n::\033[34m Cleaning device tree overlay files\033[0m"
	@echo "====================================================="
	$(MAKE) -C $(DTSDIR) clean

dtbs_install:
	@echo -e "\n::\033[34m Installing device tree overlay files\033[0m"
	@echo "====================================================="
	$(MAKE) -C $(DTSDIR) install

clean:
	$(MAKE) dtbs_clean
	$(MAKE) modules_clean
