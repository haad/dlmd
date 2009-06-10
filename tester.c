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
#include "lock.h"

/*
 * This Thread will simulate behaviour of dlm caller.
 */

void *
tester_start(void *arg)
{
	int lock_id;
	char resource[64];

	lock_id = 0;

	DPRINTF(("TEster thread started \n"));

	while(1) {
	
		printf("Waiting for user data\n");
		printf("Resource:");
		scanf("%s", resource);
	
	//	lock_resource(resource, LKM_NLMODE, LKM_BLOCK, &lock_id);
	
	//	printf("Entering Critical Section!!!\n");
	
	//	sleep(10);
	
	//	printf("Leaving Critical Section!!!\n");

	//	unlock_resource(lock_id);
		
		lock_resource(resource, LKM_CRMODE, LKM_BLOCK, &lock_id);
	
		printf("Entering CR Section!!!\n");
	
		sleep(5);
	
		printf("Leaving CR Section!!!\n");

		unlock_resource(lock_id);
		
	}
	
	return NULL;
}
