
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
 * Lock resource with name and request lock with mode. This function locks
 * a named (NUL-terminated) resource and returns thelockid if successful.
 */
int lock_resource(const char *resource, int mode, int flags, int *lockid)
{
	dlmd_lock_t *lock;
	uint64_t event;
	uint32_t type;
	
	DPRINTF(("Locking %s resource with mode %d - event %"PRIu64"\n", resource, mode, event_counter));

	/* increment event counter and return new value */
	event = dlmd_event_cnt_inc();
	type = DLMD_LOCK_LOCAL;

	if (mode == LKM_CRMODE)
		type |= DLMD_LOCK_CR;

	/* get lock structure */
	lock = dlmd_lock_add(resource, mode, event, 0, type);
	
	/* Insert lock into the queue */
	lock = dlmd_lock_insert_request(lock);

	DPRINTF(("Get Lock with %s, lock_id %" PRIu64 ", event %" PRIu64 ", active_nodes %d\n", lock->name,
		lock->lock_id, lock->event_cnt, lock->node_count));
	
	DPRINTF(("Waiting for a lock\n"));

	dlmd_lock_wait(lock);

	DPRINTF(("Entering critical section !!\n"));
	
	*lockid = lock->lock_id;
	
//	pthread_mutex_unlock(&lock->lock_mtx);
	
	return 0;
}

/* Unlock resource with lockid */
int 
unlock_resource(int lockid)
{
	
	DPRINTF(("Releasing lock %d\n", lockid));
	
	dlmd_lock_release(lockid, DLMD_LOCK_LOCAL, NULL);
		
	return 0;
}

