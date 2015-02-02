/*
 * ahocorasick.c: implementation of ahocorasick library's functions
 * This file is part of multifast.
 *
    Copyright 2010-2013 Kamiar Kanani <kamiar.kanani@gmail.com>

    multifast is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    multifast is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with multifast.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __KERNEL__
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#endif

#ifdef __KERNEL__
#include <linux/slab.h>
#include <linux/ctype.h>
#define AC_ERROR(x...) printk(x)
#define AC_PRINT(x...) printk(x)
#define AC_DEBUG(x...) {printf("ahokorasick debug: ");printf(x);}

#else

#define AC_ERROR(x...) printf(x)
#define AC_PRINT(x...) printf(x)
#define AC_DEBUG(x...) {printk("ahokorasick debug: ");printf(x);}

#endif

#include "node.h"
#include "ahocorasick.h"
#include "ac_module.h"

#define malloc(x) ac_malloc(x)
#define free(x) ac_free(x)

/* Allocation step for automata.all_nodes */
#ifndef REALLOC_CHUNK_ALLNODES
#define REALLOC_CHUNK_ALLNODES 200
#endif

/* Private function prototype */
static void ac_automata_register_nodeptr
    (AC_AUTOMATA_t * thiz, AC_NODE_t * node);
static void ac_automata_union_matchstrs
    (AC_NODE_t * node);
static void ac_automata_set_failure
    (AC_AUTOMATA_t * thiz, AC_NODE_t * node, AC_ALPHABET_t * alphas);
static void ac_automata_traverse_setfailure
    (AC_AUTOMATA_t * thiz, AC_NODE_t * node, AC_ALPHABET_t * alphas);
static void ac_automata_reset (AC_AUTOMATA_t * thiz);


/******************************************************************************
 * FUNCTION: ac_automata_init
 * Initialize automata; allocate memories and set initial values
 * PARAMS:
******************************************************************************/
AC_AUTOMATA_t * ac_automata_init ()
{
    AC_AUTOMATA_t * thiz = (AC_AUTOMATA_t *)malloc(sizeof(AC_AUTOMATA_t));
    memset (thiz, 0, sizeof(AC_AUTOMATA_t));
    thiz->root = node_create ();
    thiz->all_nodes_max = REALLOC_CHUNK_ALLNODES;
    thiz->all_nodes = (AC_NODE_t **) malloc (thiz->all_nodes_max*sizeof(AC_NODE_t *));
    ac_automata_register_nodeptr (thiz, thiz->root);
    ac_automata_reset (thiz);
    thiz->total_patterns = 0;
    thiz->automata_open = 1;
    return thiz;
}

/******************************************************************************
 * FUNCTION: ac_automata_add
 * Adds pattern to the automata.
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
 * AC_PATTERN_t * patt: the pointer to added pattern
 * RETUERN VALUE: AC_ERROR_t
 * the return value indicates the success or failure of adding action
******************************************************************************/
AC_STATUS_t ac_automata_add (AC_AUTOMATA_t * thiz, AC_PATTERN_t * patt)
{
    unsigned int i;
    AC_NODE_t * n = thiz->root;
    AC_NODE_t * next;
    AC_ALPHABET_t alpha;

    if(!thiz->automata_open)
        return ACERR_AUTOMATA_CLOSED;

    if (!patt->length)
        return ACERR_ZERO_PATTERN;

    if (patt->length > AC_PATTRN_MAX_LENGTH)
        return ACERR_LONG_PATTERN;

    for (i=0; i<patt->length; i++)
    {
        alpha = patt->astring[i];
        if ((next = node_find_next(n, alpha)))
        {
            n = next;
            continue;
        }
        else
        {
            next = node_create_next(n, alpha);
            next->depth = n->depth + 1;
            n = next;
			if(thiz->all_nodes_num >= thiz->all_nodes_max)
			    return ACERR_NUMBER_TOO_BIG;
            ac_automata_register_nodeptr(thiz, n);
        }
    }

    if(n->final)
        return ACERR_DUPLICATE_PATTERN;

    n->final = 1;
    node_register_matchstr(n, patt);
    thiz->total_patterns++;

    return ACERR_SUCCESS;
}

/******************************************************************************
 * FUNCTION: ac_automata_finalize
 * Locate the failure node for all nodes and collect all matched pattern for
 * every node. it also sorts outgoing edges of node, so binary search could be
 * performed on them. after calling this function the automate literally will
 * be finalized and you can not add new patterns to the automate.
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
******************************************************************************/
void ac_automata_finalize (AC_AUTOMATA_t * thiz)
{
    unsigned int i;
    AC_ALPHABET_t alphas[AC_PATTRN_MAX_LENGTH];
    AC_NODE_t * node;

    ac_automata_traverse_setfailure (thiz, thiz->root, alphas);

    for (i=0; i < thiz->all_nodes_num; i++)
    {
        node = thiz->all_nodes[i];
        ac_automata_union_matchstrs (node);
        node_sort_edges (node);
    }
    thiz->automata_open = 0; /* do not accept patterns any more */
}

/******************************************************************************
 * FUNCTION: ac_automata_search
 * Search in the input text using the given automata. on match event it will
 * call the call-back function. and the call-back function in turn after doing
 * its job, will return an integer value to ac_automata_search(). 0 value means
 * continue search, and non-0 value means stop search and return to the caller.
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
 * AC_TEXT_t * txt: the input text that must be searched
 * int keep: is the input text the successive chunk of the previous given text
 * void * param: this parameter will be send to call-back function. it is
 * useful for sending parameter to call-back function from caller function.
 * RETURN VALUE:
 * -1: failed; automata is not finalized
 *  0: success; input text was searched to the end
 *  1: success; input text was searched partially. (callback broke the loop)
******************************************************************************/
int ac_automata_search (AC_AUTOMATA_t * thiz, AC_TEXT_t * text, int keep, 
        AC_MATCH_CALBACK_f callback, void * param)
{
    unsigned long position;
    AC_NODE_t * current_ac;
    AC_NODE_t * next;
    AC_MATCH_t match;

    if (thiz->automata_open)
        /* you must call ac_automata_locate_failure() first */
        return -1;
    
    thiz->text = 0;

    if (!keep)
        ac_automata_reset(thiz);
        
    position = 0;
    current_ac = thiz->current_node;

    /* This is the main search loop.
     * it must be as lightweight as possible. */
    while (position < text->length)
    {
        if (!(next = node_findbs_next(current_ac, text->astring[position])))
        {
            if(current_ac->failure_node /* we are not in the root node */)
                current_ac = current_ac->failure_node;
            else
                position++;
        }
        else
        {
            current_ac = next;
            position++;
        }
		if(current_ac != current_ac->failure_node)

        if (current_ac->final && next)
        /* We check 'next' to find out if we came here after a alphabet
         * transition or due to a fail. in second case we should not report
         * matching because it was reported in previous node */
        {
            match.position = position + thiz->base_position;
            match.match_num = current_ac->matched_patterns_num;
            match.patterns = current_ac->matched_patterns;
            /* we found a match! do call-back */
            if (callback(&match, param))
                return -1;
        }
    }

    /* save status variables */
    thiz->current_node = current_ac;
    thiz->base_position += position;
    return 0;
}

/******************************************************************************
 * FUNCTION: ac_automata_settext
******************************************************************************/
void ac_automata_settext (AC_AUTOMATA_t * thiz, AC_TEXT_t * text, int keep)
{
    thiz->text = text;
    if (!keep)
        ac_automata_reset(thiz);
    thiz->position = 0;
}

/******************************************************************************
 * FUNCTION: ac_automata_findnext
******************************************************************************/
AC_MATCH_t * ac_automata_findnext (AC_AUTOMATA_t * thiz)
{
    unsigned long position;
    AC_NODE_t * current_ac;
    AC_NODE_t * next;
    static AC_MATCH_t match;
    
    if (thiz->automata_open)
        return 0;
    
    if (!thiz->text)
        return 0;
    
    position = thiz->position;
    current_ac = thiz->current_node;
    match.match_num = 0;

    /* This is the main search loop.
     * it must be as lightweight as possible. */
    while (position < thiz->text->length)
    {
        if (!(next = node_findbs_next(current_ac, thiz->text->astring[position])))
        {
            if (current_ac->failure_node /* we are not in the root node */)
                current_ac = current_ac->failure_node;
            else
                position++;
        }
        else
        {
            current_ac = next;
            position++;
        }

        if (current_ac->final && next)
        /* We check 'next' to find out if we came here after a alphabet
         * transition or due to a fail. in second case we should not report
         * matching because it was reported in previous node */
        {
            match.position = position + thiz->base_position;
            match.match_num = current_ac->matched_patterns_num;
            match.patterns = current_ac->matched_patterns;
            break;
        }
    }

    /* save status variables */
    thiz->current_node = current_ac;
    thiz->position = position;
    
    if (!match.match_num)
        /* if we came here due to reaching to the end of input text
         * not a loop break
         */
        thiz->base_position += position;
    
    return match.match_num?&match:0;
}

/******************************************************************************
 * FUNCTION: ac_automata_reset
 * reset the automata and make it ready for doing new search on a new text.
 * when you finished with the input text, you must reset automata state for
 * new input, otherwise it will not work.
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
******************************************************************************/
void ac_automata_reset (AC_AUTOMATA_t * thiz)
{
    thiz->current_node = thiz->root;
    thiz->base_position = 0;
}

/******************************************************************************
 * FUNCTION: ac_automata_release
 * Release all allocated memories to the automata
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
******************************************************************************/
void ac_automata_release (AC_AUTOMATA_t * thiz)
{
    unsigned int i;
    AC_NODE_t * n;

    for (i=0; i < thiz->all_nodes_num; i++)
    {
        n = thiz->all_nodes[i];
        node_release(n);
    }
    free(thiz->all_nodes);
    free(thiz);
}

/******************************************************************************
 * FUNCTION: ac_automata_display
 * Prints the automata to output in human readable form. it is useful for
 * debugging purpose.
 * PARAMS:
 * AC_AUTOMATA_t * thiz: the pointer to the automata
 * char repcast: 'n': print AC_REP_t as number, 's': print AC_REP_t as string
******************************************************************************/
void ac_automata_display (AC_AUTOMATA_t * thiz, char repcast)
{
    unsigned int i, j;
    AC_NODE_t * n;
    struct edge * e;
    AC_PATTERN_t sid;

    AC_PRINT("---------------------------------\n");

    for (i=0; i<thiz->all_nodes_num; i++)
    {
        n = thiz->all_nodes[i];
        AC_PRINT("NODE(%3d)/----fail----> NODE(%3d)\n",
                n->id, (n->failure_node)?n->failure_node->id:1);
        for (j=0; j<n->outgoing_degree; j++)
        {
            e = &n->outgoing[j];
            AC_PRINT("         |----(");
            if(isgraph(e->alpha))
                AC_PRINT("%c)---", e->alpha);
            else
                AC_PRINT("0x%x)", e->alpha);
            AC_PRINT("--> NODE(%3d)\n", e->next->id);
        }
        if (n->matched_patterns_num) {
            AC_PRINT("Accepted patterns: {");
            for (j=0; j<n->matched_patterns_num; j++)
            {
                sid = n->matched_patterns[j];
                if(j) AC_PRINT(", ");
                switch (repcast)
                {
                case 'n':
                    AC_PRINT("%ld", sid.rep.number);
                    break;
                case 's':
                    AC_PRINT("%s", sid.rep.stringy);
                    break;
                }
            }
            AC_PRINT("}\n");
        }
        AC_PRINT("---------------------------------\n");
    }
}

/******************************************************************************
 * FUNCTION: ac_automata_register_nodeptr
 * Adds the node pointer to all_nodes.
******************************************************************************/
static void ac_automata_register_nodeptr (AC_AUTOMATA_t * thiz, AC_NODE_t * node)
{
	/* TODO: memory reallocation */
	/*
    if(thiz->all_nodes_num >= thiz->all_nodes_max)
    {
        thiz->all_nodes_max += REALLOC_CHUNK_ALLNODES;
        thiz->all_nodes = realloc
                (thiz->all_nodes, thiz->all_nodes_max*sizeof(AC_NODE_t *));
    }
	*/
    thiz->all_nodes[thiz->all_nodes_num++] = node;
}

/******************************************************************************
 * FUNCTION: ac_automata_union_matchstrs
 * Collect accepted patterns of the node. the accepted patterns consist of the
 * node's own accepted pattern plus accepted patterns of its failure node.
******************************************************************************/
static void ac_automata_union_matchstrs (AC_NODE_t * node)
{
    unsigned int i;
    AC_NODE_t * m = node;

    while ((m = m->failure_node))
    {
        for (i=0; i < m->matched_patterns_num; i++)
            node_register_matchstr(node, &(m->matched_patterns[i]));

        if (m->final)
            node->final = 1;
    }
    // TODO : sort matched_patterns? is that necessary? I don't think so.
}

/******************************************************************************
 * FUNCTION: ac_automata_set_failure
 * find failure node for the given node.
******************************************************************************/
static void ac_automata_set_failure
    (AC_AUTOMATA_t * thiz, AC_NODE_t * node, AC_ALPHABET_t * alphas)
{
    unsigned int i, j;
    AC_NODE_t * m;

    for (i=1; i < node->depth; i++)
    {
        m = thiz->root;
        for (j=i; j < node->depth && m; j++)
            m = node_find_next (m, alphas[j]);
        if (m)
        {
            node->failure_node = m;
            break;
        }
    }
    if (!node->failure_node)
        node->failure_node = thiz->root;
}

/******************************************************************************
 * FUNCTION: ac_automata_traverse_setfailure
 * Traverse all automata nodes using DFS (Depth First Search), meanwhile it set
 * the failure node for every node it passes through. this function must be
 * called after adding last pattern to automata. i.e. after calling this you
 * can not add further pattern to automata.
******************************************************************************/
static void ac_automata_traverse_setfailure
    (AC_AUTOMATA_t * thiz, AC_NODE_t * node, AC_ALPHABET_t * alphas)
{
    unsigned int i;
    AC_NODE_t * next;

    for (i=0; i < node->outgoing_degree; i++)
    {
        alphas[node->depth] = node->outgoing[i].alpha;
        next = node->outgoing[i].next;

        /* At every node look for its failure node */
        ac_automata_set_failure (thiz, next, alphas);

        /* Recursively call itself to traverse all nodes */
        ac_automata_traverse_setfailure (thiz, next, alphas);
    }
}
