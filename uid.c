/*
 * uid.c -- UIDL handling for POP3 servers without LAST
 *
 * For license terms, see the file COPYING in this directory.
 */

#include <config.h>

#include <stdio.h>
#include <string.h>

#if defined(STDC_HEADERS)
#include <stdlib.h>
#endif

#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif

#include "fetchmail.h"

/*
 * Machinery for handling UID lists live here.  This is mainly to support
 * RFC1725-conformant POP3 servers without a LAST command, but may also be
 * useful for making the IMAP4 querying logic UID-oriented, if a future
 * revision of IMAP forces me to.  (This would be bad.  Server-side 
 * seen bits are better than UIDs, because they track messages seen by
 * *all* clients.)
 *
 * Here's the theory:
 *
 * At start of a query, we have a (possibly empty) list of UIDs to be
 * considered seen in `oldsaved'.  These are messages that were left in
 * the mailbox and *not deleted* on previous queries (we don't need to
 * remember the UIDs of deleted messages because ... well, they're gone!)
 * This list is initially set up by initialized_saved_list() from the
 * .fetchids file.
 *
 * Early in the query, during the execution of the protocol-specific 
 * getrange code, the driver expects that the host's `newsaved' member
 * will be filled with a list of UIDs and message numbers representing
 * the mailbox state.  If this list is empty, the server did
 * not respond to the request for a UID listing.
 *
 * Each time a message is fetched, we can check its UID against the
 * `oldsaved' list to see if it is old.  If not, it should be downloaded
 * (and possibly deleted).  It should be downloaded anyway if --all
 * is on.  It should not be deleted if --keep is on.
 *
 * Each time a message is deleted, we remove its id from the `newsaved'
 * member.
 *
 * At the end of the query, whatever remains in the `newsaved' member
 * (because it was not deleted) becomes the `oldsaved' list.  The old
 * `oldsaved' list is freed.
 *
 * At the end of the fetchmail run, all current `oldsaved' lists are
 * flushed out to the .fetchids file to be picked up by the next run.
 * If there are no such messages, the file is deleted.
 */

/* UIDs associated with un-queried hosts */
static struct idlist *scratchlist;

void initialize_saved_lists(hostlist, idfile)
/* read file of saved IDs and attach to each host */
struct query *hostlist;
char *idfile;
{
    int	st;
    FILE	*tmpfp;
    struct query *hostp;

    /* make sure lists are initially empty */
    for (hostp = hostlist; hostp; hostp = hostp->next)
	hostp->oldsaved = hostp->newsaved = (struct idlist *)NULL;

    /* let's get stored message UIDs from previous queries */
    if ((tmpfp = fopen(idfile, "r")) != (FILE *)NULL) {
	char buf[POPBUFSIZE+1], host[HOSTLEN+1], id[IDLEN+1];

	while (fgets(buf, POPBUFSIZE, tmpfp) != (char *)NULL)
	{
	    if ((st = sscanf(buf, "%s %s\n", host, id)) == 2)
	    {
		for (hostp = hostlist; hostp; hostp = hostp->next)
		{
		    if (strcmp(host, hostp->servername) == 0)
		    {
			save_uid(&hostp->oldsaved, -1, id);
			break;
		    }
		}

		/* if it's not in a host we're querying, save it anyway */
		if (hostp == (struct query *)NULL)
		    save_uid(&scratchlist, -1, buf);
	    }
	}
	fclose(tmpfp);
    }
}

void save_uid(idl, num, str)
/* save a number/UID pair on the given UID list */
struct idlist **idl;
int num;
char *str;
{
    struct idlist *new;

    new = (struct idlist *)xmalloc(sizeof(struct idlist));
    new->val.num = num;
    new->id = xstrdup(str);
    new->next = *idl;
    *idl = new;
}

void free_uid_list(idl)
/* free the given UID list */
struct idlist **idl;
{
    if (*idl == (struct idlist *)NULL)
	return;

    free_uid_list(&(*idl)->next);
    free ((*idl)->id);
    free(*idl);
    *idl = (struct idlist *)NULL;
}

void save_id_pair(idl, str1, str2)
/* save an ID pair on the given list */
struct idlist **idl;
char *str1, *str2;
{
    struct idlist *new;

    new = (struct idlist *)xmalloc(sizeof(struct idlist));
    new->id = xstrdup(str1);
    if (str2)
	new->val.id2 = xstrdup(str2);
    else
	new->val.id2 = (char *)NULL;
    new->next = *idl;
    *idl = new;
}

#ifdef __UNUSED__
void free_idpair_list(idl)
/* free the given ID pair list */
struct idlist **idl;
{
    if (*idl == (struct idlist *)NULL)
	return;

    free_idpair_list(&(*idl)->next);
    free ((*idl)->id);
    free ((*idl)->val.id2);
    free(*idl);
    *idl = (struct idlist *)NULL;
}
#endif

int uid_in_list(idl, str)
/* is a given ID in the given list? */
struct idlist **idl;
char *str;
{
    if (*idl == (struct idlist *)NULL || str == (char *) NULL)
	return(0);
    else if (strcmp(str, (*idl)->id) == 0)
	return(1);
    else
	return(uid_in_list(&(*idl)->next, str));
}

char *uid_find(idl, number)
/* return the id of the given number in the given list. */
struct idlist **idl;
int number;
{
    if (*idl == (struct idlist *) 0)
	return((char *) 0);
    else if (number == (*idl)->val.num)
	return((*idl)->id);
    else
	return(uid_find(&(*idl)->next, number));
}

char *idpair_find(idl, id)
/* return the id of the given number in the given list. */
struct idlist **idl;
char *id;
{
    if (*idl == (struct idlist *) 0)
	return((char *) 0);
    else if (strcmp(id, (*idl)->id) == 0)
	return((*idl)->val.id2 ? (*idl)->val.id2 : (*idl)->id);
    else
	return(idpair_find(&(*idl)->next, id));
}

int delete_uid(idl, num)
/* delete given message from given list */
struct idlist **idl;
int num;
{
    if (*idl == (struct idlist *)NULL)
	return(0);
    else if ((*idl)->val.num == num)
    {
	struct idlist	*next = (*idl)->next;

	free ((*idl)->id);
	free(*idl);
	*idl = next;
	return(1);
    }
    else
	return(delete_uid(&(*idl)->next, num));
    return(0);
}

void append_uid_list(idl, nidl)
/* append nidl to idl (does not copy *) */
struct idlist **idl;
struct idlist **nidl;
{
    if ((*idl) == (struct idlist *)NULL)
	*idl = *nidl;
    else if ((*idl)->next == (struct idlist *)NULL)
	(*idl)->next = *nidl;
    else
	append_uid_list(&(*idl)->next, nidl);
}

void update_uid_lists(hostp)
/* perform end-of-query actions on UID lists */
struct query *hostp;
{
    free_uid_list(&hostp->oldsaved);
    hostp->oldsaved = hostp->newsaved;
    hostp->newsaved = (struct idlist *) NULL;
}

void write_saved_lists(hostlist, idfile)
/* perform end-of-run write of seen-messages list */
struct query *hostlist;
char *idfile;
{
    int		idcount;
    FILE	*tmpfp;
    struct query *hostp;
    struct idlist *idp;

    /* if all lists are empty, nuke the file */
    idcount = 0;
    for (hostp = hostlist; hostp; hostp = hostp->next) {
	if (hostp->oldsaved)
	    idcount++;
    }

    /* either nuke the file or write updated last-seen IDs */
    if (!idcount)
	unlink(idfile);
    else
	if ((tmpfp = fopen(idfile, "w")) != (FILE *)NULL) {
	    for (hostp = hostlist; hostp; hostp = hostp->next) {
		for (idp = hostp->oldsaved; idp; idp = idp->next)
		    fprintf(tmpfp, "%s %s\n", hostp->servername, idp->id);
	    }
	    for (idp = scratchlist; idp; idp = idp->next)
		fputs(idp->id, tmpfp);
	    fclose(tmpfp);
	}
}

/* uid.c ends here */
