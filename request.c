#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/atomic.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <prop/proplib.h>

#include "dlmd.h"
#include "lock.h"

/*
 * This file I will keep all routines used to manipulate request/lock list.
 */

extern dlmd_node_t *local_node;

struct dlmd_lock_head lock_list;
pthread_mutex_t lock_list_mtx;

uint64_t lck_id;

static dlmd_lock_t* dlmd_lock_alloc();
static dlmd_lock_t* dlmd_lock_find_id(uint64_t, int);
static dlmd_lock_t* dlmd_lock_find_name(const char *, int);
static void dlmd_lock_destroy(dlmd_lock_t *);
static void dump_list();

static void
dump_list()
{
	dlmd_lock_t *lock;
	dlmd_node_t *node;

	printf("\n------------------------------------------------------\n");
	TAILQ_FOREACH(lock, &lock_list, next) {
		printf("Lock name %s %p\n", lock->name, lock);
		printf("Lock flags %d, type %d\n", lock->flags, lock->type);
		printf("Lock id %"PRIu64"\n", lock->lock_id);
		printf("Timestamp %"PRIu64"\n", lock->event_cnt);
		printf("Node Count %d\n", lock->node_count);
		printf("Previsious lock %p\n", TAILQ_PREV(lock, dlmd_lock_head, next));
		printf("Next lock %p\n", TAILQ_NEXT(lock, next));
		printf("First entry in list %p\n", TAILQ_FIRST(&lock_list));
		printf("Last entry in list %p\n", TAILQ_LAST(&lock_list, dlmd_lock_head));
		printf("Node list \n");
		SLIST_FOREACH(node, &lock->nodes, lock_next)
			printf("Lock node list: %s %p next: %p\n", node->node_name, node, SLIST_NEXT(node, lock_next));
		
		printf("------------------------\n");

	}
	printf("------------------------------------------------------\n\n");
}


static dlmd_lock_t *
dlmd_lock_find_name(const char *name, int type)
{
	dlmd_lock_t *lock;
	size_t slen, dlen;

	slen = strlen(name);
	TAILQ_FOREACH(lock, &lock_list, next) {
		dlen = strlen(lock->name);

		if (slen != dlen)
			continue;
		
		if ((strncmp(name, lock->name, slen) == 0) &&
			(lock->type & type))
				return lock;
	}
	
	return NULL;
}

static dlmd_lock_t *
dlmd_lock_find_id(uint64_t id, int type)
{
	dlmd_lock_t *lock;

	TAILQ_FOREACH(lock, &lock_list, next) {
		DPRINTF(("%"PRIu64"-- %"PRIu64", %d -- %d\n", lock->lock_id, id, lock->type, type));
		if (lock->lock_id == id){
				printf("FIRE!!\n");
			return lock;
		}
	}
	
	return NULL;
}

/*
 * Find lock in a global lock list, search for id and name.
 */
dlmd_lock_t *
dlmd_lock_find(const char *name, uint64_t id, int type)
{
	dlmd_lock_t *lock;

	pthread_mutex_lock(&lock_list_mtx);
	
	if (id != 0)
		if ((lock = dlmd_lock_find_id(id, type)) != NULL){
			pthread_mutex_unlock(&lock_list_mtx);
			return lock;
		}
	
	if (name != NULL)
		if ((lock = dlmd_lock_find_name(name, type)) != NULL){
			pthread_mutex_unlock(&lock_list_mtx);
			return lock;
		}	
	
	pthread_mutex_unlock(&lock_list_mtx);

	return NULL;
}

/*
 * Create Lock entry preallocate and preset.
 * Setting of lock->node and inserting to request TAILQ is left on caller.
 */
dlmd_lock_t *
dlmd_lock_add(const char *name, int flags, uint64_t event_cnt, uint32_t id, int type){
	dlmd_lock_t *lock;
	
	lock = dlmd_lock_alloc();
	
	strlcpy(lock->name, name, MAX_NAME_LEN);

	pthread_mutex_init(&lock->lock_mtx, NULL);
	pthread_cond_init(&lock->lock_cv, NULL);

	lock->flags = flags;
	lock->type = type;
	
	/* Set value of Lamport logical clock for this lock */
	lock->event_cnt = event_cnt;

	/* independent from Lamport logical timestamps */
	lock->lock_id = atomic_inc_64_nv(&lck_id);
	
	if (id == 0)
		lock->node_id = local_node->node_address.sin_addr.s_addr;
	
	SLIST_INIT(&lock->nodes);
	
	if (type & DLMD_LOCK_LOCAL)
		SLIST_INSERT_HEAD(&lock->nodes, local_node, lock_next);
		
	lock->node_count = dlmd_node_alive_count();

	return lock;
}

/*
 * Insert Lock into the request list. 
 */
dlmd_lock_t *
dlmd_lock_insert_request(dlmd_lock_t *lock)
{
	dlmd_lock_t *lock2;
	char *msg;
	size_t len;
	size_t slen, dlen;
	uint8_t concurent;
	uint32_t type;

	concurent = 0;
	type = lock->type;

	pthread_mutex_lock(&lock_list_mtx);

	slen = strlen(lock->name);
	TAILQ_FOREACH(lock2, &lock_list, next) {
		dlen = strlen(lock2->name);

		if (slen != dlen)
			continue;
		
		/* I have found same resource lock as I want to set, now I haveto test 
			if I can steal this one or I have to insert new entry into the queue */
		if ((strncmp(lock->name, lock2->name, slen) == 0)) {
		
			/* IF I want to set CR lock and it was already set go for it */
			if ((lock2->flags == LKM_CRMODE) && (lock->flags == LKM_CRMODE)) {
				printf("Found lock %s, with flag %d -> %d\n", lock->name, lock2->flags, LKM_CRMODE);
				/* Insert requesting lock node into the old one node list */
				SLIST_INSERT_HEAD(&lock2->nodes, SLIST_FIRST(&lock->nodes), lock_next);
				
				dlmd_lock_destroy(lock);
							
				lock = lock2;
				
				lock->type |= DLMD_LOCK_CR;
				goto exit;
			}
		}	
    }

	/*
	 * Search whole list for lock with same event number, if there is such lock,
	 * I need to totaly order by node_id. I need totally ordered Lamport timestamps
	 * because with only partialy ordered timestamps two nodes can ask for same 
	 * lock and enter CS.
	 */
	
	TAILQ_FOREACH(lock2, &lock_list, next) {
		/* There is event with same timestamp already */
		if (lock->event_cnt == lock2->event_cnt) {
			if (lock->node_id > lock2->node_id)
				TAILQ_INSERT_BEFORE(lock2, lock, next);
			else
				TAILQ_INSERT_AFTER(&lock_list, lock2, lock, next);
			concurent = 1;
			break;
		}		
	}
	
	/* Insert lock to the HEAD of list */
	if(concurent == 0)
		TAILQ_INSERT_HEAD(&lock_list, lock, next);
		
exit:	
	pthread_mutex_unlock(&lock_list_mtx);
	
	if (type & DLMD_LOCK_LOCAL) { 
		msg = request_msg_init(local_node->node_name, lock->name, lock->event_cnt, 
							lock->flags, lock->node_id);
	    len = strlen(msg);

		/*  Send request message to all nodes */
		dlmd_node_broadcast_msg(msg, len);

		free(msg);
	}
	return lock;
}

/*
 * Remove request from list and send unlock message to all nodes.
 * Nodes can remove lock from their lock list.
 */
int
dlmd_lock_release(uint64_t lock_id, int type, dlmd_node_t *node) 
{
	dlmd_lock_t *lock;
	dlmd_lock_t *last;
	dlmd_node_t *nodel;
	size_t len;
	uint64_t event = dlmd_event_cnt_inc();
	char *msg;
	
	pthread_mutex_lock(&lock_list_mtx);
	/* FIXME 
	 * With concurent read there is one issue in code when node A requests
	 * resource lock CR_lock. This lock is then added to queue on node B 
	 * as DLMD_LOCK_REMOTE. 
	 * When node B request CR lock on the same resource it will add node to lock 
	 * node list. 
	 *
	 * Problem is that dlmd_lock_find_id will return NULL when it is called on B 
	 * from unlock_resource because lock type doesn't match.
	 */
	 /* XXX do I need to check type for lock_id find ??? lock_id is different 
	    for all locks */
	if ((lock = dlmd_lock_find_id(lock_id, type)) == NULL)
		return ENOENT;
	
	DPRINTF(("dlmd_lock_release called %s\n", lock->name));
	
	if (type & DLMD_LOCK_LOCAL)
		node = local_node;
			
	SLIST_FOREACH (nodel, &lock->nodes, lock_next) {
		if (nodel == node)
			SLIST_REMOVE(&lock->nodes, nodel, dlmd_node, lock_next);
    }
	
	if (SLIST_EMPTY(&lock->nodes))
			TAILQ_REMOVE(&lock_list, lock, next);
	

	/* Wake up last element */
	if ((last = TAILQ_LAST(&lock_list, dlmd_lock_head)) != NULL)
		pthread_cond_signal(&last->lock_cv);
	
	pthread_mutex_unlock(&lock_list_mtx);

	if (type & DLMD_LOCK_LOCAL) {
					
		msg = unlock_msg_init(local_node->node_name, lock->name, event, lock->flags);
		len = strlen(msg);
	
		/*  Send release message to all nodes */
		dlmd_node_broadcast_msg(msg, len);

		free(msg);
	}
	
	if (SLIST_EMPTY(&lock->nodes))
		dlmd_lock_destroy(lock);

	dump_list();

	return 0;
}

/*
 * Sleep on per-lock condvar to become head of list,
 * I need two things 1) be head of a list
 *                   2) get replies from all nodes
 * to enter critical section.
 */
void
dlmd_lock_wait(dlmd_lock_t *lock)
{
	pthread_mutex_lock(&lock_list_mtx);

	DPRINTF(("dlmd_lock_wait to acquire lock %s, count %d, cv %p\n", lock->name, lock->node_count, &lock->lock_cv));
	/* wait for all replies from other locks */
	while ((lock->node_count != 0) ||
	       (TAILQ_LAST(&lock_list, dlmd_lock_head)) != lock)
		pthread_cond_wait(&lock->lock_cv, &lock_list_mtx);

	pthread_mutex_unlock(&lock_list_mtx);
}

/*
 * Wakeup thread which is sleeping on per-lock list cv.
 * Thread should sleep only when it has received replies from other nodes but
 * it is not at the head of list.
 */
void
dlmd_lock_signal(dlmd_lock_t *lock)
{
	
	pthread_mutex_lock(&lock_list_mtx);
	/*
	 * Probably better approach is use node_count for storing number of received replies
	 * and compare it to actual active node count. Problem in current system is that I
	 * can wait for reply from, node which was removed from cluster after I have requested
	 * lock.
	 *
	 * Probably best approach is implementation of better keepalive/cluster memmbership code.
	 */
			
	if (lock->node_count > 0)
		lock->node_count--;
		
	DPRINTF(("Sending signal to %s timestamp %d\n", lock->name, lock->node_count));
	if ((lock->node_count == 0) && ((TAILQ_LAST(&lock_list, dlmd_lock_head)) == lock))
		pthread_cond_signal(&lock->lock_cv);

	pthread_mutex_unlock(&lock_list_mtx);
}

dlmd_lock_t *
dlmd_lock_alloc()
{
	dlmd_lock_t *lock;
	lock = (dlmd_lock_t *)malloc(sizeof(dlmd_lock_t));
        memset(lock, '\0', sizeof(dlmd_lock_t));
	return lock;
}

void
dlmd_lock_destroy(dlmd_lock_t *lock) {
        free(lock);
}

void
dlmd_lock_init()
{
	pthread_mutex_init(&lock_list_mtx, NULL);
	TAILQ_INIT(&lock_list);
}


