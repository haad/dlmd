#ifndef _DLM_LOCK_
#define _DLM_LOCK_

/* int lock_resource(const char *resource, int mode, int flags, int *lockid);*/

/*
 * Lock modes
 */ 
#define LKM_NLMODE (1 << 0)
#define LKM_CRMODE (1 << 1)
#define	LKM_CWMODE (1 << 2)
#define	LKM_PRMODE (1 << 3)
#define	LKM_PWMODE (1 << 4)
#define	LKM_EXMODE (1 << 5)

/*
 * lock flags
 */
#define LKM_ORPHAN      0x10/* this lock is orphanable */
#define LKM_PARENTABLE  0x20/* this lock was orphaned */
#define LKM_BLOCK       0x40/* blocking lock request */
#define LKM_LOCAL       0x80/* local lock request */
#define LKM_VALBLK      0x100/* lock value block request */
#define LKM_NOQUEUE     0x200/* non blocking request */
#define LKM_CONVERT     0x400/* conversion request */

/* int lock_resource(const char *resource, int mode, int flags, int *lockid);*/
/*
 * Lock resource with name and request lock with mode. This function locks
 * a named (NUL-terminated) resource and returns thelockid if successful.
 */
int lock_resource(const char *, int, int, int *);

/* Unlock resource with lockid */
int unlock_resource(int);

/* XXX Lock Value Block ?? */

#endif
