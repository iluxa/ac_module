0.0.1alpha Aho-Corasick search framework for Linux kernel and userspace
Userspace implementation is single-thread.
Kernelspace implementation supports SMP

Build and run tests in userspace:
    $ cd userspace
    $ make
    $ ./ac_test1
    $ ./ac_test2

Build and run tests in kernel:
    $ cd kernel
    $ make
    $ sudo insmod ac_mod.ko
    $ sudo insmod ac_test1.ko
    $ sudo insmod ac_test2.ko
    $ sudo rmmod ac_test1
    $ sudo rmmod ac_test2
    $ sudo rmmod ac_mod
