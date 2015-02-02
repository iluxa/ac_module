#ifndef __KERNEL__
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#define PRINT(x...) printf(x)
#define DEBUG(x...) printf(x)
#define mdelay(x)
#else
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/sched.h>
#define PRINT(x...) printk(x)
#define DEBUG(x...) printk(x)
#endif

#include "ac_module.h"
#include "ac_test2.h"

#ifdef __KERNEL__
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aho-Corasick kernel module ac_test2");
MODULE_AUTHOR("Ilya Gavrilov <gilyav@gmail.com>");
#endif

void *urls;
ac_patterns pt1;

static void ac_test_cleanup_module( void )
{
	if(urls) {
		ac_remove_patterns(urls, &pt1);
		ac_remove_domain(urls);
	}
	ac_meminfo();
}

#ifndef __KERNEL__
int main()
#else
static int __init ac_test_init_module( void )
#endif
{
	int i,j;
	void *automata;
	ac_pattern* patt;
	void* match;

	int search_num = 1000;

	ac_meminfo();
	urls = ac_add_domain("ac_test2", 1, 2020);
	if(!urls) {
		PRINT("error adding domain\n");
		return -1;
	}

	ac_patterns_init(&pt1);

	ac_add_patterns(urls, sites2000, sites2000_num, &pt1);
#ifdef __KERNEL__
	mdelay(100);schedule();
#endif
	DEBUG("added patterns\n");
	ac_meminfo();

	PRINT("search %d patterns...\n", search_num);
    for(j = 0; j < 2 ; j++)
		for(i = 0; i < urls_array_num; i++) {

			automata = ac_get_automata(urls);
			if(!automata) {
				PRINT("Can't get_automata");
				return -1;
			}
			ac_search(automata, urls_array[i], 79);

			match = 0;
			while( (patt=ac_next_match(&match, automata, &pt1)) )
				PRINT("found matched host: %s\n", ac_pattern_str(patt));

			ac_put_automata(urls, automata);
		}

	PRINT("search %d patterns done\n", search_num);

#ifndef __KERNEL__
	ac_test_cleanup_module();
#endif

	return 0;
}

#ifdef __KERNEL__
module_init(ac_test_init_module);
module_exit(ac_test_cleanup_module);
#endif
