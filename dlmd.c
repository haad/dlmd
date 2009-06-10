

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
 * Distributed Lock Manager daemon. This program is esearch project used
 * to find how much effort will need writting functional dlm kernel/userspace
 * driver/library. To enable NetBSD in cluster enviroment and port gfs and clvmd
 * to it.
 */

/*
 * DESIGN: I will use at least 3 threads Listener, Keepalive thread.
 * KeepAlive thread will continuosly send KeepAlive messages to other nodes.
 * Listener Will Listent on socket and do all stuff needed for managing other
 *          nodes requests.
 *
 */

dlmd_conf_t conf;

static void usage(void);
static int parse_config_dict(prop_dictionary_t);

int
main(int argc, char *argv[])
{
	char ch;
	int test;
	pthread_t listener_pthread, keepalive_pthread, tester_pthread;
	
	test = 0;
	
	while ((ch = getopt(argc, argv, "c:th")) != -1 )
		switch(ch){

		case 'h':
			usage();
			/* NOTREACHED */
		break;
		case 'c':
		{
			printf("Internalizing proplib configuration file %s\n", (char *)optarg);
			conf.dict = prop_dictionary_internalize_from_file((char *)optarg);
			if (conf.dict == NULL)
				err(EXIT_FAILURE, "dlmd was not able to internalize configure file\n");

//			DPRINTF(("Configuration file\n %s", prop_dictionary_externalize(conf.dict)));
		}
		break;
		case 't':
		{
			test = 1;
		}
		break;
		default:
			usage();
			/* NOTREACHED */

		}
	argc-=optind;
	argv+=optind;

	/* Initialize node subsystem */
	dlmd_node_init();
	
	/* Initialize lock manager */
	dlmd_lock_init();
	
	pthread_mutex_init(&event_mtx, NULL);
	event_counter = 0;
	
	/* TODO force user to suply config file */
	parse_config_dict(conf.dict);

	/* Do I need something else then socket here ??? */
	pthread_create(&listener_pthread, NULL, &listener_start, &conf);
	
	pthread_create(&keepalive_pthread, NULL, &keepalive_start, &conf);
	
	if (test == 1) {
		pthread_create(&tester_pthread, NULL, &tester_start, &conf);
		pthread_detach(tester_pthread);
	}
	pthread_detach(keepalive_pthread);
	pthread_join(listener_pthread, NULL);
	
	
	return EXIT_SUCCESS;

}

static int
parse_config_dict(prop_dictionary_t dict)
{
	prop_dictionary_t node_dict;
	prop_object_iterator_t iter;
	prop_array_t array;
	
	const char *ipaddress, *local_name;
	const char *node_name, *node_ip, *node_mask;
	uint32_t port;
	size_t bits;

	iter = NULL;
		
	prop_dictionary_get_cstring_nocopy(dict, DLMDICT_LOCAL_ADDRESS,
	    &ipaddress);
	prop_dictionary_get_cstring_nocopy(dict, DLMDICT_LOCAL_NAME,
	    &local_name);
	prop_dictionary_get_uint32(dict, DLMDICT_LOCAL_PORT,
	    &port);

	bits = inet_net_pton(AF_INET, ipaddress, &conf.address.sin_addr,
	    sizeof(conf.address.sin_addr));

	/* Prepare for bind */
	conf.address.sin_port = htons(port);
	conf.address.sin_family = AF_INET;   // host byte order
	memset(conf.address.sin_zero, '\0', sizeof(conf.address.sin_zero));

        if ((conf.socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
		err(EXIT_FAILURE, "Creating socket for node %s failed\n", local_name);

	if (bind(conf.socket, (struct sockaddr *)&conf.address, sizeof(conf.address)) == -1)
		err(EXIT_FAILURE,"dlmd bind at address: %s port: %d failed \n",
		    inet_ntoa(conf.address.sin_addr), port);

	array = prop_dictionary_get(dict, DLMDICT_NODES);
	
	if ((iter = prop_array_iterator(array)) == NULL)
		warnx("Failed to create dictionary iterator no nodes defined?\n");

	/* Find other nodes and add them to node SLIST */
	while((node_dict = prop_object_iterator_next(iter)) != NULL){
		prop_dictionary_get_cstring_nocopy(node_dict, DLMDICT_NODE_NAME,
		    &node_name);
		prop_dictionary_get_cstring_nocopy(node_dict, DLMDICT_NODE_ADDRESS,
		    &node_ip);
		prop_dictionary_get_cstring_nocopy(node_dict, DLMDICT_NODE_NETMASK,
		    &node_mask);

		DPRINTF(("Node name: %s, ip address: %s, netmask: %s\n", node_name, node_ip, node_mask));

		/* Connect here doesn't need to be functional I will try to reccreat it later */
		dlmd_node_add(node_name, node_ip, node_mask, port, DLMD_NODE_TYPE_REMOTE);
	}

	/* Add local node to node list */
	dlmd_node_add(local_name, ipaddress, "0.0.0.0", port, DLMD_NODE_TYPE_LOCAL);

	DPRINTF(("DLMD is listening at IP: %s/%d, port %d\n", inet_ntoa(conf.address.sin_addr),
		bits, port));

	return 0;
}

static void
usage(void)
{
        printf("Dlmd daemon has these arguments:\n");

        exit(EXIT_SUCCESS);
}
