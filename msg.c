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
 * Initialize keepalive message buffer.
 */
char *
keepalive_msg_init(const char *name)
{
	prop_dictionary_t dict;
	char *buf;
	
	dict = prop_dictionary_create();

	prop_dictionary_set_cstring(dict, MSG_NODE_NAME, name);

	prop_dictionary_set_cstring(dict, MSG_TYPE, MSG_KEEPALIVE_TYPE);

	buf = prop_dictionary_externalize(dict);

	prop_object_release(dict);

	return buf;
}

char *
request_msg_init(const char *name, const char *resource, uint64_t event,
    uint32_t flag, uint32_t ip) {
	prop_dictionary_t dict;
	char *buf;
	
	dict = prop_dictionary_create();

	prop_dictionary_set_cstring(dict, MSG_NODE_NAME, name);

	prop_dictionary_set_cstring(dict, MSG_TYPE, MSG_LOCK_REQUEST_TYPE);
	prop_dictionary_set_cstring(dict, MSG_RESOURCE, resource);
	prop_dictionary_set_uint64(dict, MSG_EVENT, event);
	prop_dictionary_set_uint32(dict, MSG_LOCK_FLAG, flag);
	prop_dictionary_set_uint32(dict, MSG_ID, ip);
	
	buf = prop_dictionary_externalize(dict);
	prop_object_release(dict);

	return buf;
}

char *
reply_msg_init(const char *name, const char *resource, uint64_t event, uint32_t flag)
{
	prop_dictionary_t dict;
	char *buf;
	
	dict = prop_dictionary_create();

	prop_dictionary_set_cstring(dict, MSG_NODE_NAME, name);

	prop_dictionary_set_cstring(dict, MSG_TYPE, MSG_LOCK_REPLY_TYPE);
	prop_dictionary_set_cstring(dict, MSG_RESOURCE, resource);
	prop_dictionary_set_uint64(dict, MSG_EVENT, event);
	prop_dictionary_set_uint32(dict, MSG_LOCK_TYPE, flag);
	
	buf = prop_dictionary_externalize(dict);

	prop_object_release(dict);

	return buf;
}

char *
unlock_msg_init(const char *name, const char *resource, uint64_t event, uint32_t flag)
{
	prop_dictionary_t dict;
	char *buf;
	
	dict = prop_dictionary_create();

	prop_dictionary_set_cstring(dict, MSG_NODE_NAME, name);

	prop_dictionary_set_cstring(dict, MSG_TYPE, MSG_UNLOCK_TYPE);
	prop_dictionary_set_cstring(dict, MSG_RESOURCE, resource);
	prop_dictionary_set_uint64(dict, MSG_EVENT, event);
	prop_dictionary_set_uint32(dict, MSG_LOCK_TYPE, flag);
	
	buf = prop_dictionary_externalize(dict);

	prop_object_release(dict);

	return buf;
}
