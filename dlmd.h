#ifndef _DLMD_H_
#define _DLMD_H_

#include <sys/queue.h>

#include <assert.h>
#include <pthread.h>

/* 
  <dict>
	 <key>mesage_type</key>
	 <string>[1]</string>
	 <key>timestamp</key>
	 <integer>value</integer>
  </dict>
  [1] Possible message types are KeepAlive, Lock request, Lock message, Unlock message
  [2] Timestamp is Lamport Logical clock counter.
  [3] Node ID -> ip address is used as Node id (lower is better) because I need totaly ordered
      Lamport timestamps.
*/
#define MAX_NAME_LEN 128
#define DLMD_MAX_CONN 16

#ifdef DLMD_DEBUG
#define DPRINTF(arg) printf arg
#else
#define DPRINTF(arg)
#endif

#ifdef DLMD_DEBUG
#define DUMP_DICT(dict, buf) do {               \
	buf = prop_dictionary_externalize(dict);    \
	printf("%s\n", buf);								\
	free(buf);									\
} while(/*CONSTCOND*/0)
#endif

/*
 * Configuration file directives.
 */
#define DLMDICT_LOCAL_NAME    "local_name"
#define DLMDICT_LOCAL_ADDRESS "local_address"
#define DLMDICT_LOCAL_NETMASK "local_netmask"
#define DLMDICT_LOCAL_PORT    "local_port"
#define DLMDICT_NODES         "nodes"
#define DLMDICT_NODE_NAME     "name"
#define DLMDICT_NODE_ADDRESS  "address"
#define DLMDICT_NODE_NETMASK  "netmask"

/*
 * Message directives.
 */
#define MSG_TYPE                "type" /* Message type definitions. */
#define MSG_KEEPALIVE_TYPE      "keepalive"
#define MSG_LOCK_REQUEST_TYPE   "request"
#define MSG_LOCK_REPLY_TYPE     "request_reply"
#define MSG_LOCK_TYPE           "flags" /* msg_type entry */
#define MSG_LOCK_FLAG           "flags" /* type of wanted lock */
#define MSG_UNLOCK_TYPE         "unlock"
#define MSG_NODE_NAME           "node_name"
#define MSG_RESOURCE            "resource"
#define MSG_EVENT               "event" /* Lamport's logical clock. */
#define MSG_ID                  "id"    /* Node id for total ordering of Lamport timestamps */


/*
 * DLMD Structures
 */
typedef struct dlmd_conf {
	prop_dictionary_t dict;
	struct sockaddr_in address;
	int socket;
} dlmd_conf_t;

/*****************************************************************************
 * Dlmd structures there are 2 major lists in dlmd. 
 * 1) The first list manages list of active nodes know to every cluster node.
 * 2) The second list is used to store request for locks. I use Lamport mutual 
 *    exclusion algorithm to manage this list. Threrefore I need Lamport Logical
 *    clock counter for every dlmd node.
 *
 *****************************************************************************/

uint64_t event_counter; /* Lamport's Logical clock event counter */
pthread_mutex_t event_mtx; /* event_counter guard */


/*
 * Define node in a cluster, every node have this entry.
 *
 * XXX not sure what to do in dead network situations when I
 *     can't access other nodes and therefore I can synchronise
 *     access, but shared resource is available to all nodes.
 *     I need easy way howto detect this situation and what to
 *     do then. Probably best thing to do is do not access
 *     shared storage and wait for others to come up.
 */
typedef struct dlmd_node {
	char node_name[MAX_NAME_LEN];
	/* Flag is to MAX_ALIVE_MSG_LOST after alive message receive and
	   decremented after sending ALIVE message to node. When flag is < 0
	   I consider this node as disabled */
	uint32_t alive_flag;
	/* node type */
	uint32_t type;
	int node_socket;
	struct sockaddr_in node_address;
	/* list of nodes */
	pthread_mutex_t node_mtx;
	pthread_cond_t node_cv;
	SLIST_ENTRY(dlmd_node) next;
	SLIST_ENTRY(dlmd_node) lock_next;
} dlmd_node_t;

SLIST_HEAD(dlmd_node_head, dlmd_node);

dlmd_node_t *local_node;

/*
 * Lock structure for every lock in dlmd. This will become heart of dlm.
 * I use Lamport mutual eclusion algorith for managing of access to shared 
 * resources defined with dlmd_lock_t.
 *
 * If there are more than one requesters for the same lock I have to check 
 * mode, because CR and CW modes can be allowed concurently, even if their
 * event number are different.
 */
typedef struct dlmd_lock {
	struct dlmd_node_head nodes; 	/* backlink for lock owners */
	uint64_t lock_id;               /* Lock id -> used for dlm lib */
	uint64_t event_cnt;             /* Lamport logical timestamp for this lock */
	uint32_t node_id;               /* node-id so I can totaly order all locks in a cluster */
	uint32_t flags;                 /* Lock Type */
	uint32_t type;
	uint32_t node_count;		/* Set to node_count after list insertion */
	char name[MAX_NAME_LEN];
	pthread_mutex_t lock_mtx;
	pthread_cond_t  lock_cv;		
	TAILQ_ENTRY(dlmd_lock) next;
} dlmd_lock_t;

TAILQ_HEAD(dlmd_lock_head, dlmd_lock);


/* node.c */
#define MAX_ALIVE_CHECKS 3 	/* maximum number of checks before I call node disabled */
#define DLMD_NODE_TYPE_LOCAL 1
#define DLMD_NODE_TYPE_REMOTE 2
int dlmd_node_add(const char *, const char *, const char *, uint32_t, uint32_t);
int dlmd_node_broadcast_msg(const char *, size_t);
int dlmd_node_unicast_msg(dlmd_node_t *, const char *, size_t);
int dlmd_node_alive_decrement();
int dlmd_node_alive_count();
dlmd_node_t * dlmd_node_find(uint32_t, const char *);
void dlmd_node_busy(dlmd_node_t *);
void dlmd_node_unbusy(dlmd_node_t *);
void dlmd_node_init();

/* listener.c */
void * listener_start(void *);

/* keepalive.c */
void * keepalive_start(void *);

/* request.c */
#define DLMD_LOCK_LOCAL      (1 << 0)
#define DLMD_LOCK_REMOTE     (1 << 1)
#define DLMD_LOCK_CR   	     (1 << 2)

void dlmd_lock_init();
dlmd_lock_t * dlmd_lock_add(const char *, int, uint64_t, uint32_t, int);
dlmd_lock_t * dlmd_lock_find(const char *, uint64_t, int);
dlmd_lock_t * dlmd_lock_insert_request(dlmd_lock_t *);
int dlmd_lock_release(uint64_t, int, dlmd_node_t *);
void dlmd_lock_wait(dlmd_lock_t *);
void dlmd_lock_signal(dlmd_lock_t *);

/* XXX better place request.c ? */
__inline uint64_t dlmd_event_cnt_get();
__inline uint64_t dlmd_event_cnt_cas(uint64_t);
__inline uint64_t dlmd_event_cnt_inc();

/* msg.c */
char * keepalive_msg_init(const char *);
char * request_msg_init(const char *, const char *, uint64_t, uint32_t, uint32_t);
char * reply_msg_init(const char *, const char *, uint64_t, uint32_t);
char * unlock_msg_init(const char *, const char *, uint64_t, uint32_t);

/* tester.c */
void * tester_start(void *);

/******************************************************************************
 *                  Lamport logical timestamp managing routines.              *
 ******************************************************************************/

/*
 * Function for manipulating Lamport logical timestamps.
 *
 * Lamport logical timestamp algorithm:
 * 1) Before every event(sending request for lock message) increment logical
 *    timestamp by one, and include it in request message.
 * 2) After Request message receive, compare local event_counter and  logical
 *    timestamp from message and return maximum value. Which is also set as new
 *    event_counter value.
 * 3) Apply rule 1 before we insert request to list. 
 *
 */

/* Return current value of event_counter. */
static __inline uint64_t
dlmd_event_cnt_get() {
	uint64_t cnt;
	pthread_mutex_lock(&event_mtx);
	assert(!event_counter < 0);
	cnt = event_counter;
	pthread_mutex_unlock(&event_mtx);
	return cnt;
}

/* Compare and swap event_counter and value from received request, return maximal value + 1. */
static __inline uint64_t
dlmd_event_cnt_cas(uint64_t request_cnt) {
	uint64_t cnt;
	pthread_mutex_lock(&event_mtx);
	assert(event_counter >= 0);
	assert(request_cnt >= 0);

	if (event_counter < request_cnt)
		event_counter = request_cnt;

	event_counter++;
	
	cnt = event_counter;
	
	pthread_mutex_unlock(&event_mtx);

	return cnt;
}

/* Increase event_counter and return it's value */
static __inline uint64_t
dlmd_event_cnt_inc() {
	uint64_t cnt;
	pthread_mutex_lock(&event_mtx);
	assert(event_counter >= 0);
	
	cnt = (++event_counter);

	pthread_mutex_unlock(&event_mtx);

	return cnt;
}

#endif /* _DLMD_H_ */
