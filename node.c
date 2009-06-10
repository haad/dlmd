
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>

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

/*
 * Interface for manipulating with nodes which are known to dlmd.
 */

static struct dlmd_node_head node_list; /* = SLIST_HEAD_INITIALIZER(node_list);*/

static pthread_mutex_t node_list_mutex;

static dlmd_node_t* dlmd_node_alloc();
static void dlmd_node_destroy(dlmd_node_t *);
static dlmd_node_t* dlmd_node_find_ip(uint32_t);
static dlmd_node_t* dlmd_node_find_name(const char*);

static void dump_list();

static void
dump_list()
{
#ifdef DLMD_NODE_DEBUG	
	dlmd_node_t *node;

	printf("\n------------------------------------------------------\n");
	SLIST_FOREACH(node, &node_list, next) {
		printf("Node name %s %p\n", node->node_name, node);
		printf("Alive Count %d\n",node->alive_flag);
		printf("Next node %p\n", SLIST_NEXT(node, next));
		printf("First entry in list %p\n", SLIST_FIRST(&node_list));
		printf("------------------------\n");

	}
	printf("------------------------------------------------------\n\n");
#endif
}

/*
 * Find node in a global list, search for ip address and for name now.
 * XXX There is locking issue here because I doesn't guard node structure with
 *     anything before I return it. (consider usage of reference counting)
 */
dlmd_node_t *
dlmd_node_find(uint32_t ip, const char *name)
{
	dlmd_node_t *node;

	pthread_mutex_lock(&node_list_mutex);

	dump_list();

	if (ip != 0)
		if ((node = dlmd_node_find_ip(ip)) != NULL) {
			dlmd_node_busy(node);
			pthread_mutex_unlock(&node_list_mutex);
			return node;
		}

	if (name != NULL)
		if ((node = dlmd_node_find_name(name)) != NULL) {
			dlmd_node_busy(node);
			pthread_mutex_unlock(&node_list_mutex);
			return node;
		}
	
	pthread_mutex_unlock(&node_list_mutex);

	return NULL;
}

/*
 * Search for a node ip in a global list.
 */
static dlmd_node_t*
dlmd_node_find_ip(uint32_t ip)
{
	dlmd_node_t *node;

	SLIST_FOREACH(node, &node_list, next) {
		if (__IPADDR(node->node_address.sin_addr.s_addr) == ip)
			return node;
	}

	return NULL;
}


/*
 * Search for a node name in a global list.
 */
static dlmd_node_t*
dlmd_node_find_name(const char *name)
{
	dlmd_node_t *node;
	size_t slen, dlen;

	slen = strlen(name);

	SLIST_FOREACH(node, &node_list, next) {
		dlen = strlen(node->node_name);

		if (slen != dlen)
			continue;

		if (strncmp(name, node->node_name, slen) == 0)
			return node;
	}

	return NULL;
}

/*
 * Broadcast message to all active nodes in a cluster.
 */
int
dlmd_node_broadcast_msg(const char *buf, size_t buf_len)
{
	dlmd_node_t *node;
	
	pthread_mutex_lock(&node_list_mutex);
	
	dump_list();
	
	SLIST_FOREACH(node, &node_list, next) {
		if (node->alive_flag > 0 && 
			node->type != DLMD_NODE_TYPE_LOCAL)
			sendto(node->node_socket, buf, buf_len, 0,
			    (struct sockaddr *)&node->node_address, sizeof(struct sockaddr));
	}
		
	pthread_mutex_unlock(&node_list_mutex);
	
	return 0;
}

/*
 * Unicast message to node in a cluster.
 */
int
dlmd_node_unicast_msg(dlmd_node_t *node, const char *buf, size_t buf_len)
{
	pthread_mutex_lock(&node_list_mutex);
	
	dump_list();
	
	if (node->alive_flag > 0 && 
		node->type != DLMD_NODE_TYPE_LOCAL)
	sendto(node->node_socket, buf, buf_len, 0,
	    (struct sockaddr *)&node->node_address, sizeof(struct sockaddr));
			
	pthread_mutex_unlock(&node_list_mutex);
	
	return 0;
}

/*
 * 
 */
int
dlmd_node_alive_decrement()
{
	dlmd_node_t *node;
	
	pthread_mutex_lock(&node_list_mutex);
	
	dump_list();
	
	SLIST_FOREACH(node, &node_list, next) {
		if (node->alive_flag > 0 &&
			node->type != DLMD_NODE_TYPE_LOCAL)
			node->alive_flag--;
	}
		
	pthread_mutex_unlock(&node_list_mutex);
	
	return 0;
}

/*
 * Count active membersof cluster, so I know for how many replies I have to wait.
 */
int
dlmd_node_alive_count()
{
	dlmd_node_t *node;
	uint32_t cnt;

	cnt = 0;
	
	pthread_mutex_lock(&node_list_mutex);
	
	dump_list();
	
	SLIST_FOREACH(node, &node_list, next) {
		if (node->alive_flag > 0 &&
			node->type != DLMD_NODE_TYPE_LOCAL)
			cnt++;
	}	
	pthread_mutex_unlock(&node_list_mutex);
	
	return cnt;
}

/*
 * Add node entry to global list.
 */
int
dlmd_node_add(const char *name, const char *ip, const char *netmask, uint32_t port, uint32_t type)
{

	dlmd_node_t *node;
	size_t bits;
	
	node = dlmd_node_alloc();
	
	strlcpy(node->node_name, name, MAX_NAME_LEN);

	bits = inet_net_pton(AF_INET, ip, &node->node_address.sin_addr,
	    sizeof(node->node_address.sin_addr));

	/* Prepare for bind */
	node->node_address.sin_port = htons(port);
	node->node_address.sin_family = AF_INET;   // host byte order
	memset(node->node_address.sin_zero, '\0', sizeof(node->node_address.sin_zero));

	if ((node->node_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		dlmd_node_destroy(node);
		err(EXIT_FAILURE, "Creating socket to node %s failed\n", node->node_name);
	}

	node->alive_flag = -1;
	node->type = type;

	if (type == DLMD_NODE_TYPE_LOCAL)
		local_node = node;
	
	pthread_mutex_init(&node->node_mtx ,NULL);
	/*       pthread_cond_init();*/
	
	pthread_mutex_lock(&node_list_mutex);
	SLIST_INSERT_HEAD(&node_list, node, next);
	dump_list();
	pthread_mutex_unlock(&node_list_mutex);
	
	return 0;
}

void
dlmd_node_busy(dlmd_node_t *node)
{
//	pthread_mutex_lock(&node->node_mtx);
}

void
dlmd_node_unbusy(dlmd_node_t *node)
{
//	pthread_mutex_unlock(&node->node_mtx);
}	

dlmd_node_t *
dlmd_node_alloc() {
	dlmd_node_t *node;
	node = (dlmd_node_t *)malloc(sizeof(dlmd_node_t));
        memset(node, '\0', sizeof(dlmd_node_t));
	return node;
}

void
dlmd_node_destroy(dlmd_node_t *node) {
        free(node);
}

void
dlmd_node_init() {
	SLIST_INIT(&node_list);
	pthread_mutex_init(&node_list_mutex, NULL);
}
