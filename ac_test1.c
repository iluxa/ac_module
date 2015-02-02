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

#ifdef __KERNEL__
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aho-Corasick kernel module ac_test1");
MODULE_AUTHOR("Ilya Gavrilov <gilyav@gmail.com>");
#endif

void *urls;
ac_patterns pt1;
ac_patterns pt2;

static void ac_test_cleanup_module( void )
{
	if(urls) {
		ac_remove_patterns(urls, &pt1);
		ac_remove_patterns(urls, &pt2);
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
	int i;
	void *automata;
	ac_pattern* patt;
	void* match;

	const char *hosts1[] = {"microsoft.com", "amazon.com", "ebay.com"};
	int hosts_num1 = sizeof(hosts1)/sizeof(char*);

	const char *hosts2[] = {"linkedin.com", "wikipedia.org", "ebay.com", "lin"};
	int hosts_num2 = sizeof(hosts2)/sizeof(char*);

	char *urls_str[] = {"www.linkedin.com/index.html", "www.amazon.com/index.php", "www.ebay.com/index.php", "www.onecoolsite.com/travel.html"};
	int urls_num = sizeof(urls_str)/sizeof(char*);

	ac_meminfo();
	urls = ac_add_domain("ac_test1", 1, 2050);
	if(!urls) {
		PRINT("error adding domain\n");
		return -1;
	}

	ac_patterns_init(&pt1);
	ac_patterns_init(&pt2);

	ac_add_patterns(urls, hosts1, hosts_num1, &pt1);
#ifdef __KERNEL__
	mdelay(100);schedule();
#endif
	DEBUG("added patterns\n");
	ac_meminfo();

	ac_add_patterns(urls, hosts2, hosts_num2, &pt2);
#ifdef __KERNEL__
	mdelay(100);schedule();
#endif
	DEBUG("added patterns\n");
	ac_meminfo();

	for(i = 0; i < urls_num; i++) {
		automata = ac_get_automata(urls);
		if(!automata) {
			PRINT("Can't get_automata");
			return -1;
		}
		PRINT("searching in %s\n", urls_str[i]);

		ac_search(automata, urls_str[i], strlen(urls_str[i]));

        match = 0;
        while( (patt=ac_next_match(&match, automata, &pt1)) )
            PRINT("found matched host1: %s\n", ac_pattern_str(patt));

        match = 0;
        while( (patt=ac_next_match(&match, automata, &pt2)) )
            PRINT("found matched host2: %s\n", ac_pattern_str(patt));

 		ac_put_automata(urls, automata);
	}

#ifndef __KERNEL__
	ac_test_cleanup_module();
#endif

	return 0;
}

#ifdef __KERNEL__
module_init(ac_test_init_module);
module_exit(ac_test_cleanup_module);
#endif
