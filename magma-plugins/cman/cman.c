/*
  Copyright Red Hat, Inc. 2004

  The Magma Cluster API Library is free software; you can redistribute
  it and/or modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either version
  2.1 of the License, or (at your option) any later version.

  The Magma Cluster API Library is distributed in the hope that it will
  be useful, but WITHOUT ANY WARRANTY; without even the implied warranty
  of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
 */
/** @file
 * CMAN/DLM Driver - Uses locking to synchronize recovery.
 */
#include <stdint.h>
#include <magma.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <cnxman-socket.h>
#include <libdlm.h>
#include "cman-plugin.h"

#define MODULE_DESCRIPTION "CMAN/DLM Plugin v1.1"
#define MODULE_AUTHOR      "Lon Hohberger"

static int cman_lock(cluster_plugin_t *self, char *resource, int flags,
		     void **lockpp);
static int cman_unlock(cluster_plugin_t * __attribute__ ((unused)) self,
		       char *__attribute__((unused)) resource, void *lockp);
/*
 * Grab the version from the header file so we don't cause API problems
 */
IMPORT_PLUGIN_API_VERSION();


static int
cman_null(cluster_plugin_t *self)
{
	//printf("CMAN: %s called\n", __FUNCTION__);
	printf(MODULE_DESCRIPTION " NULL function called\n");
	return 0;
}


static cluster_member_list_t *
cman_member_list(cluster_plugin_t *self,
		 char __attribute__ ((unused)) *groupname)
{
	cluster_member_list_t *foo = NULL;
	struct cl_cluster_nodelist cman_nl = { 0, NULL };
	cman_priv_t *p;
	int x;
	size_t sz;

	//printf("CMAN: %s called\n", __FUNCTION__);

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(p->sockfd >= 0);

	do {
		/* Clean up if necessary */
		if (cman_nl.nodes)
			free(cman_nl.nodes);
		if (foo)
			/* Don't need to cml_free - we know we didn't
			   resolve anything */
			free(foo);

		x = ioctl(p->sockfd, SIOCCLUSTER_GETMEMBERS, NULL);
		if (x <= 0)
			return NULL;

		cman_nl.max_members = x;

		/* BIG malloc here */
		sz = sizeof(struct cl_cluster_node) * cman_nl.max_members;
		cman_nl.nodes = malloc(sz);
		assert(cman_nl.nodes != NULL);

		/* Another biggie */
		foo = cml_alloc(cman_nl.max_members);
		assert(foo != NULL);

	} while (ioctl(p->sockfd, SIOCCLUSTER_GETMEMBERS, &cman_nl) !=
		 cman_nl.max_members);

	/* Store count in our internal structure */
	p->memb_count = cman_nl.max_members;

	/* Recalc. member checksum */
	p->memb_sum = 0;
	foo->cml_count = p->memb_count;
	for (x = 0; x < p->memb_count; x++) {

		/* Copy the data to the lower layer */
		foo->cml_members[x].cm_addrs = NULL;
		foo->cml_members[x].cm_id = (uint64_t)cman_nl.nodes[x].node_id;
		p->memb_sum += foo->cml_members[x].cm_id;

		switch(cman_nl.nodes[x].state) {
		case NODESTATE_MEMBER:
			foo->cml_members[x].cm_state = STATE_UP;
			break;
		case NODESTATE_JOINING:
		case NODESTATE_DEAD:
			foo->cml_members[x].cm_state = STATE_DOWN;
			break;
		default:
			foo->cml_members[x].cm_state = STATE_INVALID;
			break;
		}
		
		strncpy(foo->cml_members[x].cm_name, cman_nl.nodes[x].name,
			sizeof(foo->cml_members[x].cm_name));
	}

	free(cman_nl.nodes);

	return foo;
}


static int
cman_quorum_status(cluster_plugin_t *self, char *groupname)
{
	int qs;
	cman_priv_t *p;

	//printf("CMAN: %s called\n", __FUNCTION__);

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(p->sockfd >= 0);

	qs = ioctl(p->sockfd, SIOCCLUSTER_ISQUORATE, NULL);

	switch(qs) {
	case 0:
	default:
		p->quorum_state = 0;
		break;
	case 1:
		p->quorum_state = QF_QUORATE | (groupname?QF_GROUPMEMBER:0);
		break;
	}
	
	return p->quorum_state;
}


static char *
cman_version(cluster_plugin_t *self)
{
	//printf("CMAN: %s called\n", __FUNCTION__);
	return MODULE_DESCRIPTION;
}


static int
cman_open(cluster_plugin_t *self)
{
	cman_priv_t *p;

	//printf("CMAN: %s called\n", __FUNCTION__);

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);

	if (p->sockfd >= 0)
		close(p->sockfd);

	p->sockfd = socket(AF_CLUSTER, SOCK_DGRAM, CLPROTO_CLIENT);
	if (p->sockfd >= 0)
		cman_quorum_status(self, NULL);

	return p->sockfd;
}


static int
cman_close(cluster_plugin_t *self, int fd)
{
	int ret;
	cman_priv_t *p;

	//printf("CMAN: %s called\n", __FUNCTION__);

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(fd == p->sockfd);

	if (p->ls)
		dlm_close_lockspace(p->ls);
	p->ls = NULL;

	ret = close(fd);
	p->sockfd = -1;

	return ret;
}


static int
cman_fence(cluster_plugin_t *self, cluster_member_t *node)
{
	int nodeid;
	cman_priv_t *p;

	//printf("CMAN: %s called\n", __FUNCTION__);

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);

	nodeid = (int)node->cm_id;

	return ioctl(p->sockfd, SIOCCLUSTER_KILLNODE, nodeid);
}


static int
cman_get_event(cluster_plugin_t *self, int fd)
{
	cluster_member_list_t *tmp;
	char lockname[64];
	void *lockp = NULL;
	cman_priv_t *p;
	int old, new;
	uint64_t oldsum;
	struct cl_portclosed_oob msg;

	new = recv(fd, &msg, sizeof(msg), MSG_OOB);

	/* Socket closed. */
	if (new == 0)
		return CE_SHUTDOWN;

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);
	assert(fd == p->sockfd);

	/*
	 * Check for Quorum transition.
 	 */
	old = p->quorum_state;
	new = cman_quorum_status(self, NULL);

	if (is_quorate(new) != is_quorate(old))
		return (is_quorate(new) ? CE_QUORATE : CE_INQUORATE);

	/*
	 * No quorum transition?  Check for a membership transition.
	 */
	oldsum = p->memb_sum;
	old = p->memb_count;
	tmp = cman_member_list(self, NULL);

	/* If the count has changed or the sum has changed but not the count,
	   then we've got a membership transition. */
	if (tmp && ((old != p->memb_count) || (oldsum != p->memb_sum))) {

		free(tmp);
		snprintf(lockname, sizeof(lockname), "magma::lock%d\n",
			 getpid());

		/*
		 * Take a DLM lock and release it so that we know fencing
		 * is complete.  The GDLM recovery happens after fencing;
		 * locks requests will block until the GDLM has recovered.
		 */
		while (cman_lock(self, lockname, CLK_EX, &lockp) != 0)
			usleep(100000);
		if (cman_unlock(self, lockname, lockp) != 0)
			return CE_NULL;

		return CE_MEMB_CHANGE;
	}

	return CE_NULL;
}


static void
ast_function(void * __attribute__ ((unused)) arg)
{
}


static int
wait_for_dlm_event(dlm_lshandle_t *ls)
{
	fd_set rfds;
	int fd = dlm_ls_get_fd(ls);

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);

	if (select(fd + 1, &rfds, NULL, NULL, NULL) == 1)
		return dlm_dispatch(fd);

	return -1;
}


static int
cman_lock(cluster_plugin_t *self,
	  char *resource,
	  int flags,
	  void **lockpp)
{
	cman_priv_t *p;
	int mode = 0, options = 0, ret = 0;
	struct dlm_lksb *lksb;

	if (!self || !lockpp) {
		errno = EINVAL;
		return -1;
	}

	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);

	/*
	 * per pjc: create/open lockspace when first lock is taken
	 */
	if (!p->ls)
		p->ls = dlm_open_lockspace("Magma");
	if (!p->ls)
		p->ls = dlm_create_lockspace("Magma", 0644);
	if (!p->ls) {
		ret = errno;
		close(p->sockfd);
		errno = ret;
		return -1;
	}

	if (flags & CLK_EX) {
		mode = LKM_EXMODE;
	} else if (flags & CLK_READ) {
		mode = LKM_PRMODE;
	} else if (flags & CLK_WRITE) {
		mode = LKM_PWMODE;
	} else {
		errno = EINVAL;
		return -1;
	}

	if (flags & CLK_NOWAIT)
		options = LKF_NOQUEUE;

	/* Allocate our lock structure. */
	lksb = malloc(sizeof(*lksb));
	assert(lksb);
	memset(lksb, 0, sizeof(*lksb));

	ret = dlm_ls_lock(p->ls, mode, lksb, options, resource,
			  strlen(resource), 0, ast_function, lksb, NULL,
			  NULL);
	if (ret != 0) {
		free(lksb);
		return ret;
	}

	if (wait_for_dlm_event(p->ls) < 0) {
		free(lksb);
		return -1;
	}

	switch(lksb->sb_status) {
	case 0:
		*lockpp = (void *)lksb;
		return 0;
	case EAGAIN:
		free(lksb);
		errno = EAGAIN;
		return -1;
	default:
		ret = lksb->sb_status;
		free(lksb);
		errno = ret;
		return -1;
	}

	/* Not reached */
	return -1;
}



static int
cman_unlock(cluster_plugin_t *self, char *__attribute__((unused)) resource,
	  void *lockp)
{
	cman_priv_t *p;
	dlm_lshandle_t ls;
	struct dlm_lksb *lksb = (struct dlm_lksb *)lockp;
	int ret;

	assert(self);
	p = (cman_priv_t *)self->cp_private.p_data;
	assert(p);
	ls = p->ls;
	assert(ls);

	if (!lockp) {
		errno = EINVAL;
		return -1;
	}

	ret = dlm_ls_unlock(ls, lksb->sb_lkid, 0, lksb, NULL);

	if (ret != 0) 
		return ret;

	/* lksb->sb_status should be EINPROG at this point */

	if (wait_for_dlm_event(p->ls) < 0) {
		errno = lksb->sb_status;
		return -1;
	}

	free(lksb);

	return ret;
}


int
cluster_plugin_load(cluster_plugin_t *driver)
{
	//printf("CMAN: %s called\n", __FUNCTION__);

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	driver->cp_ops.s_null = cman_null;
	driver->cp_ops.s_member_list = cman_member_list;
	driver->cp_ops.s_quorum_status = cman_quorum_status;
	driver->cp_ops.s_plugin_version = cman_version;
	driver->cp_ops.s_get_event = cman_get_event;
	driver->cp_ops.s_open = cman_open;
	driver->cp_ops.s_close = cman_close;
	driver->cp_ops.s_fence = cman_fence;
	driver->cp_ops.s_lock = cman_lock;
	driver->cp_ops.s_unlock = cman_unlock;

	return 0;
}


int
cluster_plugin_init(cluster_plugin_t *driver, void *priv,
		    size_t privlen)
{
	cman_priv_t *p = NULL;
	//printf("CMAN: %s called\n", __FUNCTION__);

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	if (!priv) {
		p = malloc(sizeof(*p));
		assert(p);
	} else {
		assert(privlen >= sizeof(*p));

		p = malloc(sizeof(*p));
		assert(p);
		memcpy(p, priv, sizeof(*p));
	}

	p->sockfd = -1;
	p->quorum_state = 0;
	p->memb_count = 0;

	driver->cp_private.p_data = (void *)p;
	driver->cp_private.p_datalen = sizeof(*p);

	return 0;
}


/*
 * Clear out the private data, if it exists.
 */
int
cluster_plugin_unload(cluster_plugin_t *driver)
{
	cman_priv_t *p = NULL;

	//printf("CMAN: %s called\n", __FUNCTION__);

	if (!driver) {
		errno = EINVAL;
		return -1;
	}

	assert(driver);
	p = (cman_priv_t *)driver->cp_private.p_data;
	assert(p);

	/* You did log out, right? */
	assert(p->sockfd == -1);
	free(p);
	driver->cp_private.p_data = NULL;
	driver->cp_private.p_datalen = 0;

	return 0;
}
