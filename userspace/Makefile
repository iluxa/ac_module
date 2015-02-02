MULTIFAST := ../multifast
CFLAGS = -Wall -O2 -g -MMD -I.. -I$(MULTIFAST)
SOURCES := ahocorasick.o node.o ac_module.o
TESTS := ac_test1 ac_test2

default: $(TESTS)

ac_module.o: ../ac_module.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(MULTIFAST)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

ac_test1: ../ac_test1.o $(SOURCES)
	$(CC) $(CFLAGS) $^ -o $@

ac_test2: ../ac_test2.o $(SOURCES)
	$(CC) $(CFLAGS) $^ -o $@

clean: 
	@rm -f *.o *.d *.so $(TESTS)

-include $(SOURCES:.o=.d)
