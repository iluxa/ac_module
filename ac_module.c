/**
 *  Aho-Corasick search framework
 *  compiles as linux kernel module for SMP or as single-thread userspace library
 *  compiles with modified multifast-v1.4.2 (C) Kamiar Kanani <kamiar.kanani@gmail.com>
 *  (C) 2015 Ilya Gavrilov <gilyav@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 *
 */
#ifdef __KERNEL__
#include <linux/module.h>
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#include <linux/printk.h>
#define AC_ERROR(x...) printk(x)
/*#define AC_ERROR_RATELIMIT(x...) printk_ratelimited(KERN_INFO x)*/
#define AC_ERROR_RATELIMIT(x...) printk(x)
#define AC_PRINT(x...) printk(x)
#define AC_DEBUG(x...) /*{printk("ac_module debug: ");printk(x);}*/
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <malloc.h>
#define AC_ERROR(x...) printf(x)
#define AC_ERROR_RATELIMIT(x...) printf(x)
#define AC_PRINT(x...) printf(x)
#define AC_DEBUG(x...) /*{printf("ac_module debug: ");printf(x);}*/
#define EXPORT_SYMBOL_GPL(x)
#endif

#include "list.h"
#include "ahocorasick.h"
#include "ac_module.h"

#ifndef __KERNEL__
int nr_cpu_ids = 1;
int get_cpu() { return 0; }
void put_cpu() {}
#endif

#ifndef __KERNEL__
typedef int atomic_t;
int atomic_read(atomic_t* val) {return *val;}
void atomic_set(atomic_t* val, int set_val) {*val = set_val;}
int atomic_inc(atomic_t* val) {return *val += 1;}
int atomic_dec(atomic_t* val) {return *val -= 1;}
int atomic_dec_and_test(atomic_t* val) {*val -= 1; return val==0;}
int atomic_add_unless(atomic_t *val, int inc, int val_cmp)
{
	if(*val != val_cmp)
	{
		*val += inc;
		return 1;
	}
	return 0;
}
#endif

#ifndef atomic_inc_zero
#define atomic_inc_zero(v)          atomic_add_unless((v), 1, 1)
#endif

#ifdef __KERNEL__
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Aho-Corasick framework kernel module with multifast-v1.4.2 core");
MODULE_AUTHOR("Ilya Gavrilov <gilyav@gmail.com>");

static DEFINE_SPINLOCK(domains_lock);
#endif
#define AC_PATTERNS_HSIZE 200 /* TODO: use as param */

struct pattern {
    int num;
	int use_count;
#ifdef __KERNEL__
	spinlock_t lock;
#endif
	char *pattern;
};

struct ac_match {
    struct list_head list;
    int num;
};

struct domain;

struct automata {
	struct list_head list;

	struct domain *domain;
	int id;
	AC_AUTOMATA_t *atm;
	uint8_t ignorecase; /* ascii only*/
	uint8_t dirty; /* need rebuild */
	uint8_t freed; /* freed atms should be moved from leased list to free list by thier owner cpu only */
	atomic_t use;
#ifdef __KERNEL__
	struct work_struct work;
#endif
    struct list_head match;
};

struct automatas_pool
{
	struct list_head free;
	struct list_head leased;
	uint8_t rebuilding;
};

struct domain {
	struct list_head list;
#ifdef __KERNEL__
	spinlock_t lock;
#endif
	int id;
	char name[80];
	struct pattern *patterns;
	unsigned patterns_number;
	struct automatas_pool *automatas;
	unsigned automatas_number;
#ifdef __KERNEL__
	struct workqueue_struct *wq;
#endif
};

LIST_HEAD(domains);
static int domain_id = 0;

void ac_free_automatas(void * domain_id);
int __ac_clean_patterns(void * domain_id);
#ifdef __KERNEL__
static void __ac_automata_rebuild( struct work_struct *work);
#else
void __ac_automata_rebuild(struct automata *atm);
#endif
int __ac_automatas_rebuild(struct list_head *automatas, struct pattern *patterns, unsigned patt_num, int cpu);
int __ac_domain_rebuild(struct domain *dom);
void __ac_set_bit(uint8_t *mask, int n);
int __ac_test_bit(uint8_t *mask, int n);
inline void __ac_clear_bit(uint8_t *mask, int n); 

void *ac_add_domain(const char* domain, unsigned automatas_number, unsigned patterns_number, int ignorecase)
{
	int i, j;
	struct domain *dom;
	struct automata *atm;

	list_for_each_entry(dom, &domains, list) {
		if(strcmp(dom->name, domain) == 0 ) {
			AC_ERROR("Domain %s already exists\n", domain);
			return NULL;
		}
	}
	dom = (struct domain*)ac_zmalloc(sizeof(struct domain));
	if(!dom)
	{
		AC_ERROR("Error allocating domain %s\n", domain);
		return NULL;
	}
	strncpy(dom->name, domain, 80);
	dom->id = domain_id++;
	dom->patterns = ac_zmalloc(sizeof(struct pattern)*patterns_number);
	if(!dom->patterns) {
		ac_remove_domain(dom);
		AC_ERROR("Error allocating domain paterns for %s\n", domain);
		return NULL;
	}
	dom->patterns_number = patterns_number;
	dom->automatas_number = automatas_number;
	for(i = 0; i < dom->patterns_number; i++) {
#ifdef __KERNEL__
		spin_lock_init(&dom->patterns[i].lock);
#endif
        dom->patterns[i].num = i;
    }
	dom->automatas = (struct automatas_pool*) ac_zmalloc(nr_cpu_ids*sizeof(struct automatas_pool));
	if(!dom->automatas) {
		ac_remove_domain(dom);
		AC_ERROR("Error allocating domain queues for %s\n", domain);
		return NULL;
	}
	for(i = 0; i < nr_cpu_ids ; i++ ) {
		INIT_LIST_HEAD(&dom->automatas[i].free);
		INIT_LIST_HEAD(&dom->automatas[i].leased);
	}
#ifdef __KERNEL__
	dom->wq = alloc_workqueue(dom->name, 0, 1);
	if(!dom->wq)
	{
		ac_remove_domain(dom);
		AC_ERROR("Error allocating workqueues for %s\n", domain);
		return NULL;
	}
	spin_lock_init(&dom->lock);
#endif
	for(i = 0; i < nr_cpu_ids ; i++ )
		for(j = 0; j < automatas_number; j++) {
			atm = ac_zmalloc(sizeof(*atm));
			if(!atm) {
				ac_remove_domain(dom);
				return NULL;
			}
			atm->id = j;
			atm->domain = dom;
			atm->ignorecase = ignorecase;
			atm->atm = ac_automata_init(atm->ignorecase);
			ac_automata_finalize(atm->atm);
			list_add_tail(&atm->list, &dom->automatas[i].free);
#ifdef __KERNEL__
			INIT_WORK( &atm->work, __ac_automata_rebuild );
#endif
            INIT_LIST_HEAD( &atm->match );
		}

#ifdef __KERNEL__
	spin_lock(&domains_lock);
#endif
	list_add_tail(&dom->list, &domains);
#ifdef __KERNEL__
	spin_unlock(&domains_lock);
#endif

	return dom;
}
EXPORT_SYMBOL_GPL(ac_add_domain);

int ac_remove_domain(void * domain_id)
{
	struct domain *dom = (struct domain *)domain_id;
	struct automata *atm;
	struct automata *atm_safe;
	int i;

	AC_DEBUG("ac_remove_domain: remove domain %s(%p)\n", dom->name, dom);
	ac_free_automatas(domain_id);

	/* automatas_leased must be empty here */
	for(i = 0; i < nr_cpu_ids ; i++ )
		if(!list_empty(&dom->automatas[i].leased)) {
			AC_ERROR("Domain %s is busy\n", dom->name);
			return -1;
		}

#ifdef __KERNEL__
	if(!spin_trylock_bh(&dom->lock))
		return -EBUSY;
	spin_lock(&domains_lock);
#endif
	list_del(&dom->list);
#ifdef __KERNEL__
	spin_unlock(&domains_lock);
	spin_unlock_bh(&dom->lock);
	if(dom->wq)
		destroy_workqueue( dom->wq );
#endif
	for(i = 0; i < nr_cpu_ids ; i++ )
		list_for_each_entry_safe(atm, atm_safe, &dom->automatas[i].free, list) {
			list_del(&atm->list);
			if(atm->atm)
				ac_automata_release(atm->atm);
			ac_free(atm);
		}

	ac_free(dom->automatas);

	if(dom->patterns) {
	    __ac_clean_patterns(dom);
		ac_free(dom->patterns);
	}

	ac_free(dom);
	return 0;
}
EXPORT_SYMBOL_GPL(ac_remove_domain);

int ac_patterns_init(ac_patterns *patt)
{
    int i;
    *patt = ac_malloc(sizeof(**patt) * AC_PATTERNS_HSIZE);
    if(*patt == 0)
        return -ENOMEM;

    for(i = 0; i < AC_PATTERNS_HSIZE; i++)
        INIT_HLIST_HEAD(*patt+i);

	return 0;
}
EXPORT_SYMBOL_GPL(ac_patterns_init);

int ac_add_patterns(void * domain_id, const char *patts[], unsigned patterns_num, ac_patterns* patterns)
{
	struct domain *dom = (struct domain *)domain_id;
	struct pattern *patt;
	struct pattern *patt_free;
	ac_pattern *entry;
	const char *pattern;
	char *pattern_str;
	uint8_t need_rebuild = 0;
	int i;
	int j;
	int ret = 0;

#ifdef __KERNEL__
	if(!spin_trylock_bh(&dom->lock))
		return -EBUSY;
#endif
	for(j=0; j<patterns_num; j++)
	{
		pattern = patts[j];
		patt = NULL;
		patt_free = NULL;
		for(i = 0; i < dom->patterns_number; i++) {
			patt = &dom->patterns[i];
			if(!patt_free && patt->use_count == 0)
				patt_free = patt;
			if(patt->pattern && (strcmp(patt->pattern, pattern) == 0))
				break;
			patt = NULL;
		}
		if(!patt) {
			if(!patt_free) {
				ret = -ENOMEM;
				break;
			}
			patt = patt_free;
			pattern_str = ac_malloc_atomic(strlen(pattern)+1);
			if(!pattern_str) {
				ret = -ENOMEM;
				break;
			}

#ifdef __KERNEL__
			spin_lock_bh(&patt->lock);
#endif
			if(patt->pattern)
				ac_free(patt->pattern);
			patt->pattern = pattern_str;
			strcpy(patt->pattern, pattern);
#ifdef __KERNEL__
			spin_unlock_bh(&patt->lock);
#endif
			need_rebuild = 1;
		}
		entry = ac_zmalloc_atomic(sizeof(*entry));
		if(!entry) {
			ret = -ENOMEM;
			break;
		}
		/* TODO: check whether need memory barrier here */
		++patt->use_count;
		entry->pattern = patt;

		hlist_add_head(&entry->list, *patterns + patt->num % AC_PATTERNS_HSIZE);
	}
	if(need_rebuild)
		__ac_domain_rebuild(dom);
#ifdef __KERNEL__
	spin_unlock_bh(&dom->lock);
#endif
	return ret;
}
EXPORT_SYMBOL_GPL(ac_add_patterns);

int ac_remove_patterns(void * domain_id, ac_patterns *patterns)
{
	struct domain *dom = (struct domain *)domain_id;
	ac_pattern *entry;
    struct hlist_node *n;
	struct pattern *patt;
	uint8_t need_rebuild = 0;
    int i;

#ifdef __KERNEL__
	if(!spin_trylock_bh(&dom->lock))
		return -EBUSY;
#endif
    for(i = 0; i < AC_PATTERNS_HSIZE; i++) {
        hlist_for_each_entry_safe(entry, n, *patterns+i, list) {
            hlist_del(&entry->list);
            patt = entry->pattern;
            ac_free(entry);
            if(--patt->use_count == 0)
                need_rebuild = 1;
            AC_DEBUG("ac_remove_patterns: num: %d use_count: %d\n", patt->num, patt->use_count);
        }
    }
	if(need_rebuild)
		__ac_domain_rebuild(dom);
#ifdef __KERNEL__
	spin_unlock_bh(&dom->lock);
#endif
    ac_free(*patterns);
	return 0;
}
EXPORT_SYMBOL_GPL(ac_remove_patterns);

void ac_free_automata(void * domain_id, int cpu)
{
	struct domain *dom = (struct domain *)domain_id;
	struct automata *atm;
	struct automata *atm_safe;

	list_for_each_entry_safe(atm, atm_safe, &dom->automatas[cpu].leased, list)
		if( atm->freed ) {
			list_del(&atm->list);
			atm->freed = 0;
			atomic_dec(&atm->use);
			AC_DEBUG("ac_free_automata: atm: %p rebuilding: %d\n", atm, dom->automatas[cpu].rebuilding);
			if(dom->automatas[cpu].rebuilding)
				atm->dirty = 1;
			if(atm->dirty) {
#ifdef __KERNEL__
				if( !queue_work_on(cpu, dom->wq, &atm->work) )
					AC_ERROR("ac_free_automata error queue work\n");
#else
				__ac_automata_rebuild(atm);
#endif
			}
			list_add_tail(&atm->list, &dom->automatas[cpu].free);
		}
	dom->automatas[cpu].rebuilding = 0;
}

void ac_free_automatas(void * domain_id)
{
	int i;
	for(i = 0; i < nr_cpu_ids ; i++ ) {
		ac_free_automata(domain_id, i);
	}
}

void *ac_get_automata(void * domain_id)
{
	struct domain *dom = (struct domain *)domain_id;
	struct automata *atm = NULL;
	struct automata *atm_safe = NULL;
    struct ac_match *match;
    struct ac_match *match_safe;

	int cpu = get_cpu();
	ac_free_automata(domain_id, cpu);
	list_for_each_entry_safe(atm, atm_safe, &dom->automatas[cpu].free, list) {
		if(atomic_inc_zero(&atm->use)) {
			AC_DEBUG("ac_get_automata: got atm: %p\n", atm);
			list_del(&atm->list);
			list_add_tail(&atm->list, &dom->automatas[cpu].leased);
            list_for_each_entry_safe(match, match_safe, &atm->match, list) {
                list_del(&match->list);
                ac_free(match);
            }
			break;
		}
		atm = NULL;
	}
	put_cpu();
	return atm;
}
EXPORT_SYMBOL_GPL(ac_get_automata);

void ac_put_automata(void * domain_id, void *automata)
{
	struct automata *atm = (struct automata*)automata;

	AC_DEBUG("ac_put_automata: put atm: %p\n", atm);
	atm->freed = 1;
}
EXPORT_SYMBOL_GPL(ac_put_automata);

int __ac_match_handler (AC_MATCH_t * matchp, void * param)
{
    unsigned int j;
	struct automata *atm = (struct automata*)param;
    struct ac_match *match;
    for (j=0; j < matchp->match_num; j++) {
        AC_DEBUG ("\t__ac_match_handler %lu (%s)\n", matchp->patterns[j].rep.number, matchp->patterns[j].astring);
		/* TODO: if pattern changed since search started - do not mark it here */
        match = ac_malloc_atomic(sizeof(*match));
        if(!match)
        {
            AC_ERROR_RATELIMIT("ac_module: out of memory\n");
            return -1;
        }
        match->num = matchp->patterns[j].rep.number;
        list_add_tail(&match->list, &atm->match);
	}

    return 0;
}

int ac_search(void *automata, const void *data, unsigned len)
{
    AC_TEXT_t input_text;
	struct automata *atm = (struct automata*)automata;
	input_text.astring = data;
	input_text.length = len;

	return ac_automata_search(atm->atm, &input_text, 1, __ac_match_handler, automata);
}
EXPORT_SYMBOL_GPL(ac_search);

ac_pattern* ac_next_match(void **patt_match, void *automata, ac_patterns *patterns)
{
    struct ac_match **match = (struct ac_match **)patt_match;
	struct automata *atm = (struct automata*)automata;
    ac_pattern *patt;

    if(list_empty(&atm->match))
        return NULL;

    if(*match == 0)
        *match = list_prepare_entry((*match), &atm->match, list);
    list_for_each_entry_continue((*match), &atm->match, list) {
        hlist_for_each_entry(patt, (*patterns + (*match)->num % AC_PATTERNS_HSIZE), list) {
            if((*match)->num == ((struct pattern*)patt->pattern)->num)
                return patt;
        }
    }

    return NULL;
}
EXPORT_SYMBOL_GPL(ac_next_match);

const char * ac_pattern_str(ac_pattern *pattern)
{
	struct pattern *patt = (struct pattern*)pattern->pattern;
	return patt->pattern;
}
EXPORT_SYMBOL_GPL(ac_pattern_str);

int __ac_clean_patterns(void * domain_id)
{
	struct domain *dom = (struct domain *)domain_id;
	unsigned i;

	for(i = 0; i < dom->patterns_number ; i++) {
#ifdef __KERNEL__
		spin_lock_bh(&dom->patterns[i].lock);
#endif
		if( dom->patterns[i].pattern ) {
			ac_free( dom->patterns[i].pattern );
			dom->patterns[i].pattern = NULL;
		}
#ifdef __KERNEL__
		spin_unlock_bh(&dom->patterns[i].lock);
#endif
	}

	return 0;
}

#ifdef __KERNEL__
static void __ac_automata_rebuild( struct work_struct *work)
#else
void __ac_automata_rebuild(struct automata *atm)
#endif
{
	AC_PATTERN_t pattern;
	AC_STATUS_t ac_status;
	unsigned i;

#ifdef __KERNEL__
	struct automata *atm = container_of(work, struct automata, work);
#endif
	struct pattern* patterns = atm->domain->patterns;
	unsigned patt_num = atm->domain->patterns_number;

	if(!atomic_inc_zero(&atm->use))
	{
	    AC_DEBUG("__ac_automatas_rebuild exits on busy\n");
		return;
	}

	if(atm->atm)
		ac_automata_release(atm->atm);
	atm->atm = ac_automata_init(atm->ignorecase);
	for(i = 0; i < patt_num; i++) {
		if(patterns[i].use_count == 0)
			continue;
#ifdef __KERNEL__
		spin_lock_bh(&patterns[i].lock);
#endif
		pattern.astring = patterns[i].pattern;
		pattern.length = strlen(patterns[i].pattern);
		pattern.rep.number = i;
		ac_status = ac_automata_add(atm->atm, &pattern);
		if(ac_status != ACERR_SUCCESS) {
			AC_ERROR("__ac_automatas_rebuild: wrong status %d for pattern %s. Skip it.\n", ac_status, patterns[i].pattern);
		}
#ifdef __KERNEL__
		spin_unlock_bh(&patterns[i].lock);
#endif
	}
	ac_automata_finalize(atm->atm);
	atm->dirty = 0;
	atomic_dec(&atm->use);
}

int __ac_automatas_rebuild(struct list_head *automatas, struct pattern *patterns, unsigned patt_num, int cpu)
{
	struct automata *atm;

	list_for_each_entry(atm, automatas, list) {
		atm->dirty = 1;
		AC_DEBUG("queue rebuild cpu: %d atm:%p\n", cpu, atm);
#ifdef __KERNEL__
		if( !queue_work_on(cpu, atm->domain->wq, &atm->work) ) {
			AC_ERROR("__ac_automatas_rebuild error queue work\n");
			return -1;
		}
#else
		__ac_automata_rebuild(atm);
#endif
	}

	return 0;
}

int __ac_domain_rebuild(struct domain *dom)
{
	int i;
	for(i = 0; i < nr_cpu_ids ; i++ ) {
		__ac_automatas_rebuild(&dom->automatas[i].free, dom->patterns, dom->patterns_number, i);
		dom->automatas[i].rebuilding = 1;
	}

	return 0;
}

void __ac_set_bit(uint8_t *mask, int n) 
{
	mask[n/8] |= (1 << (n%8));
}
int __ac_test_bit(uint8_t *mask, int n) 
{
	return mask[n/8] & (1 << (n%8));
}
inline void __ac_clear_bit(uint8_t *mask, int n) 
{
	mask[n/8] &= ~(1 << (n%8));
}

atomic_t __ac_alloc;
atomic_t __ac_free;
atomic_t __ac_max_alloc;

inline void __ac_calc_max_alloc(void)
{
	if(atomic_read(&__ac_alloc) - atomic_read(&__ac_free) 
			> atomic_read(&__ac_max_alloc)) {
		atomic_set(&__ac_max_alloc, atomic_read(&__ac_alloc) - atomic_read(&__ac_free));
	}
}
inline void *__ac_malloc(size_t sz, int gfp)
{
	void *ret = 
#ifdef __KERNEL__
	kmalloc(sz, gfp);
#else
	malloc(sz);
#endif

#ifdef __KERNEL__
	atomic_add(ksize(ret), &__ac_alloc);
#else
	__ac_alloc += malloc_usable_size(ret);
#endif
	__ac_calc_max_alloc();
	return ret;
}

inline void *ac_malloc(size_t sz)
{
#ifdef __KERNEL__
    return __ac_malloc(sz, GFP_KERNEL);
#else
    return __ac_malloc(sz, 0);
#endif
}

inline void *ac_malloc_atomic(size_t sz)
{
#ifdef __KERNEL__
    return __ac_malloc(sz, GFP_ATOMIC);
#else
    return __ac_malloc(sz, 0);
#endif
}

inline void *__ac_zmalloc(size_t sz, int gfp)
{
	void *ret = 
#ifdef __KERNEL__
	kzalloc(sz, gfp);
#else
	malloc(sz);
	if(!ret)
		return NULL;
	bzero(ret, sz);
#endif
	if(ret) {
#ifdef __KERNEL__
		atomic_add(ksize(ret), &__ac_alloc);
#else
		__ac_alloc += malloc_usable_size(ret);
#endif
		__ac_calc_max_alloc();
	}
	return ret;
}

inline void *ac_zmalloc(size_t sz)
{
#ifdef __KERNEL__
    return __ac_zmalloc(sz, GFP_KERNEL);
#else
    return __ac_zmalloc(sz, 0);
#endif
}

inline void *ac_zmalloc_atomic(size_t sz)
{
#ifdef __KERNEL__
    return __ac_zmalloc(sz, GFP_ATOMIC);
#else
    return __ac_zmalloc(sz, 0);
#endif
}

inline void ac_free(void *ptr)
{
#ifdef __KERNEL__
	atomic_add(ksize(ptr), &__ac_free);
	kfree(ptr);
#else
	__ac_free += malloc_usable_size(ptr);
	free(ptr);
#endif
}

void ac_meminfo(void)
{
	int ac_alloc = atomic_read(&__ac_alloc);
	int ac_free = atomic_read(&__ac_free);
	int max_alloc = atomic_read(&__ac_max_alloc);
	AC_PRINT("meminfo: alloc: %d free: %d max_alloc: %d use:%d\n", ac_alloc, ac_free, max_alloc, ac_alloc-ac_free);
#ifndef __KERNEL__
	/* malloc_stats(); */
#endif
}
EXPORT_SYMBOL_GPL(ac_meminfo);

#ifdef __KERNEL__
static int __init ac_init_module( void )
{
	return 0;
}
static void ac_cleanup_module( void )
{
}
module_init(ac_init_module);
module_exit(ac_cleanup_module);
#endif

