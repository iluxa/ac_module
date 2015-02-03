/**
 *  Aho-Corasick search framework header file
 *  compiles as linux kernel module for SMP or as single-thread userspace library
 *  compiles with modified multifast-v1.4.2 (C) Kamiar Kanani <kamiar.kanani@gmail.com>
 *  (C) 2015 Ilya Gavrilov <gilyav@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation.
 *
 */

#ifndef __KERNEL__
#include "list.h"
#else
#include <linux/list.h>
#include <linux/smp.h>
#include <linux/cpumask.h>
#include <linux/slab.h>
#endif

#define REALLOC_CHUNK_ALLNODES 20000

typedef struct {
	struct hlist_node list;
	void *pattern;
} ac_pattern;

typedef struct hlist_head* ac_patterns;

/**
 * ac_add_domain - create new domain 
 * @doman - domain name
 * @automatas_number - number of automatas for each cpu for this domain
 * @patterns_number - maximum patterns number can be added to this domain
 * @ignorecase - case unsensitive search inside domain (ascii only)
 * 
 * @return - pointer to domain or NULL on error
 */
void * ac_add_domain(const char* domain, unsigned automatas_number, unsigned patterns_number, int ignorecase);

/**
 * ac_remove_domain - delete domain
 * domain_id - pointer to domain
 *
 * @return 0 on success, < 0 on errnor
 */
int ac_remove_domain(void * domain_id);

/**
 * ac_patterns_init - init patterns bundle before using
 * @patt - pointer to pattern bundle
 *
 * @return 0 on success, < 0 on error
 */
int ac_patterns_init(ac_patterns *patt);

/**
 * ac_add_patterns - add patterns to patterns bundle
 * @domain_id - pointer to domain for add
 * @patts - char array of patterns
 * @patterns_num - size of patts
 * @patterns - pointer to pattern bundle
 *
 * @return 0 on success, < 0 on error
 *
 * ac_patterns_init must be called for patterns before first ac_add_patterns call
 */
int ac_add_patterns(void * domain_id, const char *patts[], unsigned patterns_num, ac_patterns* patterns);

/**
 * ac_remove_patterns - remove patterns bundle from domain
 * @domain_id - pointer to domain for remove
 * @patterns - pointer to pattern bundle
 *
 * @return 0 on success, < 0 on error
 *
 * after putterns bundle remove ac_patterns_init must be use to reinit patterns
 */
int ac_remove_patterns(void * domain_id, ac_patterns *patterns);

/**
 * ac_get_automata - get automata from domain to search
 * @domain - domain id
 *
 * @return automata ready to search or NULL of no free automata available
 */
void *ac_get_automata(void * domain_id);

/**
 * ac_put_automata - release automata after search
 *
 * @domain - domain id
 * @automata - automata to release
 */
void ac_put_automata(void * domain_id, void *automata);

/**
 * ac_search - put new portion of data to atomata
 *
 * @automata - automata id
 * @data - pointer to available data
 * @len length of data in bytes
 *
 * @return -1 on error, 0 on success
 */
int ac_search(void *automata, const void *data, unsigned len);

/**
 * ac_next_match - returns next matched ac_pattern in pattern of automata
 *
 * @patt_match - pointer to last match pointer
 * @automata - automata id
 * @patterns - patterns bundle
 *
 * @return found pattern or NULL
 * 
 * example to call:
 * ac_pattern *patt;
 * void *match = 0;
 * while( (patt=ac_next_match(&match, automata, &pt)) )
 *    PRINT(ac_pattern_str(patt));
 */
ac_pattern* ac_next_match(void **patt_match, void *automata, ac_patterns *patterns);

/**
 * ac_pattern_str - get character string from pattern str
 * @pattern - ac_pattern returned by ac_next_match
 *
 * @return null-terminated char string corresponded to pattern
 */
const char * ac_pattern_str(ac_pattern *pattern);

inline void *ac_malloc(size_t sz);
inline void *ac_malloc_atomic(size_t sz);
inline void *ac_zmalloc(size_t sz);
inline void *ac_zmalloc_atomic(size_t sz);
inline void ac_free(void *ptr);
void ac_meminfo(void);
