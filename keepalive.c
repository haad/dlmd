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

#define KEEP_ALIVE_INT 300

/*
 * This file will contain all routines used in sender thread.
 */
void *
keepalive_start(void *arg)
{
	dlmd_conf_t *conf = (dlmd_conf_t *)arg;
	const char *name;
	char *buf;
	
	prop_dictionary_get_cstring_nocopy(conf->dict, DLMDICT_LOCAL_NAME,
	    &name);

	if ((buf = keepalive_msg_init(name)) == NULL){
		DPRINTF(("Unable to create message buffer."));
		pthread_exit(NULL);
	}
		
	while (1) {
		/*
		 * Broadcast message to all active cluster nodes every 300 seconds.
		 */
		dlmd_node_broadcast_msg(buf, strlen(buf));
		/*
		 * Decrement alive flag after I have sent alive message to them. If I receive
		 * keepalive message from node I will increment flag. 
		 */
		dlmd_node_alive_decrement();
		
		sleep(KEEP_ALIVE_INT);
	}
	
	return NULL;
}

