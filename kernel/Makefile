KDIR = /lib/modules/`uname -r`/build 
PWD = $(shell pwd) 
TARGET = ac_mod
TEST1 = ac_test1
TEST2 = ac_test2
ccflags-y = -I$(src)/../ -I$(src)/../multifast/
obj-m   := $(TARGET).o $(TEST1).o $(TEST2).o
ac_mod-y := ../multifast/ahocorasick.o ../multifast/node.o ../ac_module.o
ac_test1-y := ../ac_test1.o
ac_test2-y := ../ac_test2.o

kernel_module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules 

clean: 
	@rm -f *.o .*.cmd .*.flags *.mod.c *.order
	@rm -fR .tmp* 
	@rm -rf .tmp_versions 
	@rm *.ko *.symvers

