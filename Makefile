PROG=           dlmd
MAN=		#defined
WARN= 		4
SRCS=		dlmd.c node.c listener.c keepalive.c lock.c request.c tester.c msg.c

BINDIR=         /sbin

CFLAGS+=        -fno-inline -Wall -g -DDLMD_DEBUG

CPPFLAGS+=  -I. -I${LIBDM_INCLUDE}

LDADD+= 	-lprop

LDADD+=		-lpthread

.include <bsd.prog.mk>
