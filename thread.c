/*
 * Copyright (C) 1996-2000 Michael R. Elkins <me@cs.hmc.edu>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 */ 

#include "mutt.h"
#include "sort.h"

#include <string.h>
#include <ctype.h>

#define VISIBLE(hdr, ctx) (hdr->virtual >= 0 || (hdr->collapsed && (!ctx->pattern || hdr->limited)))

/* determine whether a is a descendant of b */
static int is_descendant (THREAD *a, THREAD *b)
{
  while (a)
  {
    if (a == b)
      return (1);
    a = a->parent;
  }
  return (0);
}

/* Determines whether to display a message's subject. */
static int need_display_subject (CONTEXT *ctx, HEADER *hdr)
{
  THREAD *tmp, *tree = hdr->thread;

  /* if our subject is different from our parent's, display it */
  if (hdr->subject_changed)
    return (1);

  /* if our subject is different from that of our closest previously displayed
   * sibling, display the subject */
  for (tmp = tree->prev; tmp; tmp = tmp->prev)
  {
    hdr = tmp->message;
    if (hdr && VISIBLE (hdr, ctx))
    {
      if (hdr->subject_changed)
	return (1);
      else
	break;
    }
  }
  
  /* if there is a parent-to-child subject change anywhere between us and our
   * closest displayed ancestor, display the subject */
  for (tmp = tree->parent; tmp; tmp = tmp->parent)
  {
    hdr = tmp->message;
    if (hdr)
    {
      if (VISIBLE (hdr, ctx))
	return (0);
      else if (hdr->subject_changed)
	return (1);
    }
  }
  
  /* if we have no visible parent or previous sibling, display the subject */
  return (1);
}

/* determines whether a later sibling or the child of a later 
 * sibling is displayed.  
 */

static int is_next_displayed (CONTEXT *ctx, THREAD *tree)
{
  int depth = 0;
  HEADER *hdr;

  if ((tree = tree->next) == NULL)
    return (0);

  FOREVER
  {
    hdr = tree->message;
    if (hdr && VISIBLE (hdr, ctx))
      return (1);

    if (tree->child)
    {
      tree = tree->child;
      depth++;
    }
    else
    {
      while (!tree->next && depth > 0)
      {
	tree = tree->parent;
	depth--;
      }
      if ((tree = tree->next) == NULL)
	break;
    }
  }
  return (0);
}


/* Since the graphics characters have a value >255, I have to resort to
 * using escape sequences to pass the information to print_enriched_string().
 * These are the macros M_TREE_* defined in mutt.h.
 *
 * ncurses should automatically use the default ASCII characters instead of
 * graphics chars on terminals which don't support them (see the man page
 * for curs_addch).
 */
void mutt_linearize_tree (CONTEXT *ctx, int linearize)
{
  char *pfx = NULL, *mypfx = NULL, *arrow = NULL, *myarrow = NULL;
  char corner = Sort & SORT_REVERSE ? M_TREE_ULCORNER : M_TREE_LLCORNER;
  int depth = 0, start_depth = 0, max_depth = 0, max_width = 0;
  int nextdisp = 0, visible;
  THREAD *tree = ctx->tree;
  HEADER **array = ctx->hdrs + (Sort & SORT_REVERSE ? ctx->msgcount - 1 : 0);
  HEADER *hdr;

  FOREVER
  {
    hdr = tree->message;

    if (hdr)
    {
      if ((visible = VISIBLE (hdr, ctx)) !=  0)
	hdr->display_subject = need_display_subject (ctx, hdr);

      safe_free ((void **) &hdr->tree);
    }
    else
      visible = 0;

    if (depth >= max_depth)
      safe_realloc ((void **) &pfx,
		    (max_depth += 32) * 2 * sizeof (char));

    if (depth - start_depth >= max_width)
      safe_realloc ((void **) &arrow,
		    (max_width += 16) * 2 * sizeof (char));

    if (depth)
    {
      myarrow = arrow + (depth - start_depth - (start_depth ? 0 : 1)) * 2;
      nextdisp = is_next_displayed (ctx, tree);
      
      if (depth && start_depth == depth)
	myarrow[0] = nextdisp ? M_TREE_LTEE : corner;
      else
	myarrow[0] = tree->parent->message ? M_TREE_HIDDEN : M_TREE_MISSING;
      myarrow[1] = (tree->fake_thread ?  M_TREE_STAR
		    : (tree->duplicate_thread ? M_TREE_EQUALS : M_TREE_HLINE));
      if (visible)
      {
	myarrow[2] = M_TREE_RARROW;
	myarrow[3] = 0;
      }

      if (visible)
      {
	hdr->tree = safe_malloc ((2 + depth * 2) * sizeof (char));
	if (start_depth > 1)
	{
	  strncpy (hdr->tree, pfx, (start_depth - 1) * 2);
	  strfcpy (hdr->tree + (start_depth - 1) * 2,
		   arrow, (2 + depth - start_depth) * 2);
	}
	else
	  strfcpy (hdr->tree, arrow, 2 + depth * 2);
      }
    }

    if (linearize && hdr)
    {
      *array = hdr;
      array += Sort & SORT_REVERSE ? -1 : 1;
    }

    if (tree->child)
    {
      if (depth)
      {
	mypfx = pfx + (depth - 1) * 2;
	mypfx[0] = nextdisp ? M_TREE_VLINE : M_TREE_SPACE;
	mypfx[1] = M_TREE_SPACE;
      }
      if (depth || !option (OPTHIDEMISSING)
	  || tree->message || tree->child->next)
	depth++;
      if (visible)
        start_depth = depth;
      tree = tree->child;
      hdr = tree->message;
    }
    else
    {
      while (!tree->next && tree->parent)
      {
	if (hdr && VISIBLE (hdr, ctx))
	  start_depth = depth;
	tree = tree->parent;
	hdr = tree->message;
	if (depth)
	{
	  if (start_depth == depth)
	    start_depth--;
	  depth--;
	}
      }
      if (hdr && VISIBLE (hdr, ctx))
	start_depth = depth;
      tree = tree->next;
      if (!tree)
	break;
    }
  }

  safe_free ((void **) &pfx);
  safe_free ((void **) &arrow);
}

/* since we may be trying to attach as a pseudo-thread a THREAD that
 * has no message, we have to make a list of all the subjects of its
 * most immediate existing descendants.  we also note the earliest
 * date on any of the parents and put it in *dateptr. */
static LIST *make_subject_list (THREAD *cur, time_t *dateptr)
{
  THREAD *start = cur;
  ENVELOPE *env;
  time_t thisdate;
  LIST *curlist, *oldlist, *newlist, *subjects = NULL;
  int rc = 0;
  
  FOREVER
  {
    while (!cur->message)
      cur = cur->child;

    if (dateptr)
    {
      thisdate = option (OPTTHREADRECEIVED)
	? cur->message->received : cur->message->date_sent;
      if (!*dateptr || thisdate < *dateptr)
	*dateptr = thisdate;
    }

    env = cur->message->env;
    if (env->real_subj &&
	((env->real_subj != env->subject) || (!option (OPTSORTRE))))
    {
      for (curlist = subjects, oldlist = NULL;
	   curlist; oldlist = curlist, curlist = curlist->next)
      {
	rc = mutt_strcmp (env->real_subj, curlist->data);
	if (rc >= 0)
	  break;
      }
      if (!curlist || rc > 0)
      {
	newlist = safe_calloc (1, sizeof (LIST));
	newlist->data = env->real_subj;
	if (oldlist)
	{
	  newlist->next = oldlist->next;
	  oldlist->next = newlist;
	}
	else
	{
	  newlist->next = subjects;
	  subjects = newlist;
	}
      }
    }

    while (!cur->next && cur != start)
    {
      cur = cur->parent;
    }
    if (cur == start)
      break;
    cur = cur->next;
  }

  return (subjects);
}

/* find the best possible match for a parent mesage based upon subject.
 * if there are multiple matches, the one which was sent the latest, but
 * before the current message, is used. 
 */
static THREAD *find_subject (CONTEXT *ctx, THREAD *cur)
{
  struct hash_elem *ptr;
  THREAD *tmp, *last = NULL;
  int hash;
  LIST *subjects = NULL, *oldlist;
  time_t date = 0;  

  subjects = make_subject_list (cur, &date);

  while (subjects)
  {
    hash = hash_string ((unsigned char *) subjects->data,
			ctx->subj_hash->nelem);
    for (ptr = ctx->subj_hash->table[hash]; ptr; ptr = ptr->next)
    {
      tmp = ((HEADER *) ptr->data)->thread;
      if (tmp != cur &&			   /* don't match the same message */
	  !tmp->fake_thread &&		   /* don't match pseudo threads */
	  tmp->message->subject_changed && /* only match interesting replies */
	  !is_descendant (tmp, cur) &&	   /* don't match in the same thread */
	  (date >= (option (OPTTHREADRECEIVED) ?
		    tmp->message->received :
		    tmp->message->date_sent)) &&
	  (!last ||
	   (option (OPTTHREADRECEIVED) ?
	    (last->message->received < tmp->message->received) :
	    (last->message->date_sent < tmp->message->date_sent))) &&
	  tmp->message->env->real_subj &&
	  mutt_strcmp (subjects->data, tmp->message->env->real_subj) == 0)
	last = tmp; /* best match so far */
    }

    oldlist = subjects;
    subjects = subjects->next;
    safe_free ((void **) &oldlist);
  }
  return (last);
}

/* remove cur and its descendants from their current location.
 * also make sure ancestors of cur no longer are sorted by the
 * fact that cur is their descendant. */
static void unlink_message (THREAD **old, THREAD *cur)
{
  THREAD *tmp;

  if (cur->prev)
    cur->prev->next = cur->next;
  else
    *old = cur->next;

  if (cur->next)
    cur->next->prev = cur->prev;

  if (cur->sort_key)
  {
    for (tmp = cur->parent; tmp && tmp->sort_key == cur->sort_key;
	 tmp = tmp->parent)
      tmp->sort_key = NULL;
  }
}

/* add cur as a prior sibling of *new, with parent newparent */
static void insert_message (THREAD **new, THREAD *newparent, THREAD *cur)
{
  if (*new)
    (*new)->prev = cur;

  cur->parent = newparent;
  cur->next = *new;
  cur->prev = NULL;
  *new = cur;
}

/* thread by subject things that didn't get threaded by message-id */
static void pseudo_threads (CONTEXT *ctx)
{
  THREAD *tree = ctx->tree, *top = tree;
  THREAD *tmp, *cur, *parent, *curchild, *nextchild;

  if (!ctx->subj_hash)
    ctx->subj_hash = mutt_make_subj_hash (ctx);

  while (tree)
  {
    cur = tree;
    tree = tree->next;
    if ((parent = find_subject (ctx, cur)) != NULL)
    {
      cur->fake_thread = 1;
      unlink_message (&top, cur);
      insert_message (&parent->child, parent, cur);
      parent->sort_children = 1;
      tmp = cur;
      FOREVER
      {
	while (!tmp->message)
	  tmp = tmp->child;

	/* if the message we're attaching has pseudo-children, they
	 * need to be attached to its parent, so move them up a level.
	 * but only do this if they have the same real subject as the
	 * parent, since otherwise they rightly belong to the message
	 * we're attaching. */
	if (tmp == cur
	    || !mutt_strcmp (tmp->message->env->real_subj,
			     parent->message->env->real_subj))
	{
	  tmp->message->subject_changed = 0;

	  for (curchild = tmp->child; curchild; )
	  {
	    nextchild = curchild->next;
	    if (curchild->fake_thread)
	    {
	      unlink_message (&tmp->child, curchild);
	      insert_message (&parent->child, parent, curchild);
	    }
	    curchild = nextchild;
	  }
	}

	while (!tmp->next && tmp != cur)
	{
	  tmp = tmp->parent;
	}
	if (tmp == cur)
	  break;
	tmp = tmp->next;
      }
    }
  }
  ctx->tree = top;
}


void mutt_clear_threads (CONTEXT *ctx)
{
  int i;

  for (i = 0; i < ctx->msgcount; i++)
  {
    ctx->hdrs[i]->thread = NULL;
    ctx->hdrs[i]->threaded = 0;
  }
  ctx->tree = NULL;

  if (ctx->thread_hash)
    hash_destroy (&ctx->thread_hash, *free);
}

int compare_threads (const void *a, const void *b)
{
  static sort_t *sort_func = NULL;

  if (a || b)
    return ((*sort_func) (&(*((THREAD **) a))->sort_key,
			  &(*((THREAD **) b))->sort_key));
  /* a hack to let us reset sort_func even though we can't
   * have extra arguments because of qsort
   */
  else
  {
    sort_func = NULL;
    sort_func = mutt_get_sort_func (Sort);
    return (sort_func ? 1 : 0);
  }
}

THREAD *mutt_sort_subthreads (THREAD *thread, int init)
{
  THREAD **array, *sort_key, *top, *tmp;
  HEADER *oldsort_key;
  int i, array_size, sort_top = 0;
  
  /* we put things into the array backwards to save some cycles,
   * but we want to have to move less stuff around if we're 
   * resorting, so we sort backwards and then put them back
   * in reverse order so they're forwards
   */
  Sort ^= SORT_REVERSE;
  if (!compare_threads (NULL, NULL))
    return (thread);

  top = thread;

  array = safe_malloc ((array_size = 256) * sizeof (THREAD *));
  while (1)
  {
    if (init || !thread->sort_key)
    {
      thread->sort_key = NULL;

      if (thread->parent)
        thread->parent->sort_children = 1;
      else
	sort_top = 1;
    }

    if (thread->child)
    {
      thread = thread->child;
      continue;
    }
    else
    {
      /* if it has no children, it must be real. sort it on its own merits */
      thread->sort_key = thread->message;

      if (thread->next)
      {
	thread = thread->next;
	continue;
      }
    }

    while (!thread->next)
    {
      /* if it has siblings and needs to be sorted, sort it... */
      if (thread->prev && (thread->parent ? thread->parent->sort_children : sort_top))
      {
	/* put them into the array */
	for (i = 0; thread; i++, thread = thread->prev)
	{
	  if (i >= array_size)
	    safe_realloc ((void **) &array,
			  (array_size *= 2) * sizeof (THREAD *));

	  array[i] = thread;
	}

	qsort ((void *) array, i, sizeof (THREAD *), *compare_threads);

	/* attach them back together.  make thread the last sibling. */
	thread = array[0];
	thread->next = NULL;
	array[i - 1]->prev = NULL;

	if (thread->parent)
	  thread->parent->child = array[i - 1];
	else
	  top = array[i - 1];

	while (--i)
	{
	  array[i - 1]->prev = array[i];
	  array[i]->next = array[i - 1];
	}
      }

      if (thread->parent)
      {
	tmp = thread;
	thread = thread->parent;

	if (!thread->sort_key || thread->sort_children)
	{
	  /* make sort_key the first or last sibling, as appropriate */
	  sort_key = (!(Sort & SORT_LAST) ^ !(Sort & SORT_REVERSE)) ? thread->child : tmp;

	  /* we just sorted its children */
	  thread->sort_children = 0;

	  oldsort_key = thread->sort_key;
	  thread->sort_key = thread->message;

	  if (Sort & SORT_LAST)
	  {
	    if (!thread->sort_key
		|| ((((Sort & SORT_REVERSE) ? 1 : -1)
		     * compare_threads ((void *) &thread,
					(void *) &sort_key))
		    > 0))
	      thread->sort_key = sort_key->sort_key;
	  }
	  else if (!thread->sort_key)
	    thread->sort_key = sort_key->sort_key;

	  /* if its sort_key has changed, we need to resort it and siblings */
	  if (oldsort_key != thread->sort_key)
	  {
	    if (thread->parent)
	      thread->parent->sort_children = 1;
	    else
	      sort_top = 1;
	  }
	}
      }
      else
      {
	Sort ^= SORT_REVERSE;
	safe_free ((void **) &array);
	return (top);
      }
    }

    thread = thread->next;
  }
}

static void check_subjects (CONTEXT *ctx, int init)
{
  HEADER *cur;
  THREAD *tmp;
  int i;

  for (i = 0; i < ctx->msgcount; i++)
  {
    cur = ctx->hdrs[i];
    if (cur->thread->check_subject)
      cur->thread->check_subject = 0;
    else if (!init)
      continue;

    /* figure out which messages have subjects different than their parents' */
    tmp = cur->thread->parent;
    while (tmp && !tmp->message)
    {
      tmp = tmp->parent;
    }

    if (!tmp)
      cur->subject_changed = 1;
    else if (cur->env->real_subj && tmp->message->env->real_subj)
      cur->subject_changed = mutt_strcmp (cur->env->real_subj,
					  tmp->message->env->real_subj) ? 1 : 0;
    else
      cur->subject_changed = (cur->env->real_subj
			      || tmp->message->env->real_subj) ? 1 : 0;
  }
}

void mutt_sort_threads (CONTEXT *ctx, int init)
{
  HEADER *cur;
  int i, oldsort, using_refs = 0;
  THREAD *thread, *new, *tmp, top;
  LIST *ref = NULL;
  
  /* set Sort to the secondary method to support the set sort_aux=reverse-*
   * settings.  The sorting functions just look at the value of
   * SORT_REVERSE
   */
  oldsort = Sort;
  Sort = SortAux;
  
  if (!ctx->thread_hash)
    init = 1;

  if (init)
    ctx->thread_hash = hash_create (ctx->msgcount * 2);

  /* we want a quick way to see if things are actually attached to the top of the
   * thread tree or if they're just dangling, so we attach everything to a top
   * node temporarily */
  top.parent = top.next = top.prev = NULL;
  top.child = ctx->tree;
  for (thread = ctx->tree; thread; thread = thread->next)
    thread->parent = &top;

  /* put each new message together with the matching messageless THREAD if it
   * exists.  otherwise, if there is a THREAD that already has a message, thread
   * new message as an identical child.  if we didn't attach the message to a
   * THREAD, make a new one for it. */
  for (i = 0; i < ctx->msgcount; i++)
  {
    cur = ctx->hdrs[i];

    if (!cur->thread)
    {
      if ((!init || option (OPTDUPTHREADS)) && cur->env->message_id)
	thread = hash_find (ctx->thread_hash, cur->env->message_id);
      else
	thread = NULL;

      if (thread && !thread->message)
      {
	/* this is a message which was missing before */
	thread->message = cur;
	cur->thread = thread;
	thread->check_subject = 1;

	/* mark descendants as needing subject_changed checked */
	for (tmp = (thread->child ? thread->child : thread); tmp != thread; )
	{
	  while (!tmp->message)
	    tmp = tmp->child;
	  tmp->check_subject = 1;
	  while (!tmp->next && tmp != thread)
	    tmp = tmp->parent;
	  if (tmp != thread)
	    tmp = tmp->next;
	}

	if (thread->parent)
	{
	  /* remove threading info above it based on its children, which we'll
	   * recalculate based on its headers.  make sure not to leave
	   * dangling missing messages.  note that we haven't kept track
	   * of what info came from its children and what from its siblings'
	   * children, so we just remove the stuff that's definitely from it */
	  do
	  {
	    tmp = thread->parent;
	    unlink_message (&tmp->child, thread);
	    thread->parent = NULL;
	    thread->sort_key = NULL;
	    thread->fake_thread = 0;
	    thread = tmp;
	  } while (thread != &top && !thread->child && !thread->message);
	}
      }
      else
      {
	new = (option (OPTDUPTHREADS) ? thread : NULL);

	thread = safe_calloc (1, sizeof (THREAD));
	thread->message = cur;
	thread->check_subject = 1;
	cur->thread = thread;
	hash_insert (ctx->thread_hash,
		     cur->env->message_id ? cur->env->message_id : "",
		     thread, 1);

	if (new)
	{
	  if (new->duplicate_thread)
	    new = new->parent;

	  thread = cur->thread;

	  insert_message (&new->child, new, thread);
	  thread->duplicate_thread = 1;
	  thread->message->threaded = 1;
	}
      }
    }
    else
    {
      /* unlink pseudo-threads because they might be children of newly
       * arrived messages */
      thread = cur->thread;
      for (new = thread->child; new; )
      {
	tmp = new->next;
	if (new->fake_thread)
	{
	  unlink_message (&thread->child, new);
	  insert_message (&top.child, &top, new);
	  new->fake_thread = 0;
	}
	new = tmp;
      }
    }
  }

  /* thread by references */
  for (i = 0; i < ctx->msgcount; i++)
  {
    cur = ctx->hdrs[i];
    if (cur->threaded)
      continue;
    cur->threaded = 1;

    thread = cur->thread;
    using_refs = 0;

    while (1)
    {
      if (using_refs == 0)
      {
	/* look at the beginning of in-reply-to: */
	if ((ref = cur->env->in_reply_to) != NULL)
	  using_refs = 1;
	else
	{
	  ref = cur->env->references;
	  using_refs = 2;
	}
      }
      else if (using_refs == 1)
      {
	/* if there's no references header, use all the in-reply-to:
	 * data that we have.  otherwise, use the first reference
	 * if it's different than the first in-reply-to, otherwise use
	 * the second reference (since at least eudora puts the most
	 * recent reference in in-reply-to and the rest in references)
	 */
	if (!cur->env->references)
	  ref = ref->next;
	else
	{
	  if (mutt_strcmp (ref->data, cur->env->references->data))
	    ref = cur->env->references;
	  else
	    ref = cur->env->references->next;
	  
	  using_refs = 2;
	}
      }
      else
	ref = ref->next; /* go on with references */
      
      if (!ref)
	break;

      if ((new = hash_find (ctx->thread_hash, ref->data)) == NULL)
      {
	new = safe_calloc (1, sizeof (THREAD));
	hash_insert (ctx->thread_hash, ref->data, new, 1);
      }
      else
      {
	if (new->duplicate_thread)
	  new = new->parent;
	if (is_descendant (new, thread)) /* no loops! */
	  break;
      }

      if (thread->parent)
	unlink_message (&top.child, thread);
      insert_message (&new->child, new, thread);
      thread = new;
      if (thread->message || (thread->parent && thread->parent != &top))
	break;
    }

    if (!thread->parent)
      insert_message (&top.child, &top, thread);
  }

  /* detach everything from the temporary top node */
  for (thread = top.child; thread; thread = thread->next)
  {
    thread->parent = NULL;
  }
  ctx->tree = top.child;

  check_subjects (ctx, init);

  if (!option (OPTSTRICTTHREADS))
    pseudo_threads (ctx);

  ctx->tree = mutt_sort_subthreads (ctx->tree, init);

  /* restore the oldsort order. */
  Sort = oldsort;

  /* Put the list into an array. */
  mutt_linearize_tree (ctx, 1);
}

static HEADER *find_virtual (THREAD *cur)
{
  THREAD *top;

  if (cur->message && cur->message->virtual >= 0)
    return (cur->message);

  top = cur;
  if ((cur = cur->child) == NULL)
    return (NULL);

  FOREVER
  {
    if (cur->message && cur->message->virtual >= 0)
      return (cur->message);

    if (cur->child)
      cur = cur->child;
    else if (cur->next)
      cur = cur->next;
    else
    {
      while (!cur->next)
      {
	cur = cur->parent;
	if (cur == top)
	  return (NULL);
      }
      cur = cur->next;
    }
    /* not reached */
  }
}

int _mutt_aside_thread (HEADER *hdr, short dir, short subthreads)
{
  THREAD *cur;
  HEADER *tmp;

  if ((Sort & SORT_MASK) != SORT_THREADS)
  {
    mutt_error _("Threading is not enabled.");
    return (hdr->virtual);
  }

  cur = hdr->thread;

  if (!subthreads)
  {
    while (cur->parent)
      cur = cur->parent;
  }
  else
  {
    if ((dir != 0) ^ ((Sort & SORT_REVERSE) != 0))
    {
      while (!cur->next && cur->parent)
	cur = cur->parent;
    }
    else
    {
      while (!cur->prev && cur->parent)
	cur = cur->parent;
    }
  }

  if ((dir != 0) ^ ((Sort & SORT_REVERSE) != 0))
  {
    do
    { 
      cur = cur->next;
      if (!cur)
	return (-1);
      tmp = find_virtual (cur);
    } while (!tmp);
  }
  else
  {
    do
    { 
      cur = cur->prev;
      if (!cur)
	return (-1);
      tmp = find_virtual (cur);
    } while (!tmp);
  }

  return (tmp->virtual);
}

int mutt_parent_message (CONTEXT *ctx, HEADER *hdr)
{
  THREAD *thread;

  if ((Sort & SORT_MASK) != SORT_THREADS)
  {
    mutt_error _("Threading is not enabled.");
    return (hdr->virtual);
  }

  for (thread = hdr->thread->parent; thread; thread = thread->parent)
  {
    if ((hdr = thread->message) != NULL)
    {
      if (VISIBLE (hdr, ctx))
	return (hdr->virtual);
      else
      {
	mutt_error _("Parent message is not visible in this limited view.");
	return (-1);
      }
    }
  }
  
  mutt_error _("Parent message is not available.");
  return (-1);
}

void mutt_set_virtual (CONTEXT *ctx)
{
  int i;
  HEADER *cur;

  ctx->vcount = 0;
  ctx->vsize = 0;

  for (i = 0; i < ctx->msgcount; i++)
  {
    cur = ctx->hdrs[i];
    if (cur->virtual >= 0)
    {
      cur->virtual = ctx->vcount;
      ctx->v2r[ctx->vcount] = i;
      ctx->vcount++;
      ctx->vsize += cur->content->length + cur->content->offset - cur->content->hdr_offset;
      cur->num_hidden = mutt_get_hidden (ctx, cur);
    }
  }
}

int _mutt_traverse_thread (CONTEXT *ctx, HEADER *cur, int flag)
{
  THREAD *thread, *top;
  HEADER *roothdr = NULL;
  int final, reverse = (Sort & SORT_REVERSE), minmsgno;
  int num_hidden = 0, new = 0, old = 0;
  int min_unread_msgno = INT_MAX, min_unread = cur->virtual;
#define CHECK_LIMIT (!ctx->pattern || cur->limited)

  if ((Sort & SORT_MASK) != SORT_THREADS && !(flag & M_THREAD_GET_HIDDEN))
  {
    mutt_error (_("Threading is not enabled."));
    return (cur->virtual);
  }

  final = cur->virtual;
  thread = cur->thread;
  while (thread->parent)
    thread = thread->parent;
  top = thread;
  while (!thread->message)
    thread = thread->child;
  cur = thread->message;
  minmsgno = cur->msgno;

  if (!cur->read && CHECK_LIMIT)
  {
    if (cur->old)
      old = 2;
    else
      new = 1;
    if (cur->msgno < min_unread_msgno)
    {
      min_unread = cur->virtual;
      min_unread_msgno = cur->msgno;
    }
  }

  if (cur->virtual == -1 && CHECK_LIMIT)
    num_hidden++;

  if (flag & (M_THREAD_COLLAPSE | M_THREAD_UNCOLLAPSE))
  {
    cur->pair = 0; /* force index entry's color to be re-evaluated */
    cur->collapsed = flag & M_THREAD_COLLAPSE;
    if (cur->virtual != -1)
    {
      roothdr = cur;
      if (flag & M_THREAD_COLLAPSE)
	final = roothdr->virtual;
    }
  }

  if ((thread = thread->child) == NULL)
  {
    /* return value depends on action requested */
    if (flag & (M_THREAD_COLLAPSE | M_THREAD_UNCOLLAPSE))
      return (final);
    else if (flag & M_THREAD_UNREAD)
      return ((old && new) ? new : (old ? old : new));
    else if (flag & M_THREAD_GET_HIDDEN)
      return (num_hidden);
    else if (flag & M_THREAD_NEXT_UNREAD)
      return (min_unread);
  }
  
  FOREVER
  {
    cur = thread->message;

    if (cur)
    {
      if (flag & (M_THREAD_COLLAPSE | M_THREAD_UNCOLLAPSE))
      {
	cur->pair = 0; /* force index entry's color to be re-evaluated */
	cur->collapsed = flag & M_THREAD_COLLAPSE;
	if (!roothdr && CHECK_LIMIT)
	{
	  roothdr = cur;
	  if (flag & M_THREAD_COLLAPSE)
	    final = roothdr->virtual;
	}

	if (reverse && (flag & M_THREAD_COLLAPSE) && (cur->msgno < minmsgno) && CHECK_LIMIT)
	{
	  minmsgno = cur->msgno;
	  final = cur->virtual;
	}

	if (flag & M_THREAD_COLLAPSE)
	{
	  if (cur != roothdr)
	    cur->virtual = -1;
	}
	else 
	{
	  if (CHECK_LIMIT)
	    cur->virtual = cur->msgno;
	}
      }


      if (!cur->read && CHECK_LIMIT)
      {
	if (cur->old)
	  old = 2;
	else
	  new = 1;
	if (cur->msgno < min_unread_msgno)
	{
	  min_unread = cur->virtual;
	  min_unread_msgno = cur->msgno;
	}
      }

      if (cur->virtual == -1 && CHECK_LIMIT)
	num_hidden++;
    }

    if (thread->child)
      thread = thread->child;
    else if (thread->next)
      thread = thread->next;
    else
    {
      int done = 0;
      while (!thread->next)
      {
	thread = thread->parent;
	if (thread == top)
	{
	  done = 1;
	  break;
	}
      }
      if (done)
	break;
      thread = thread->next;
    }
  }

  /* return value depends on action requested */
  if (flag & (M_THREAD_COLLAPSE | M_THREAD_UNCOLLAPSE))
    return (final);
  else if (flag & M_THREAD_UNREAD)
    return ((old && new) ? new : (old ? old : new));
  else if (flag & M_THREAD_GET_HIDDEN)
    return (num_hidden+1);
  else if (flag & M_THREAD_NEXT_UNREAD)
    return (min_unread);

  return (0);
#undef CHECK_LIMIT
}


/* if flag is 0, we want to know how many messages
 * are in the thread.  if flag is 1, we want to know
 * our position in the thread. */
int mutt_messages_in_thread (HEADER *hdr, int flag)
{
  THREAD *threads[2];
  int i;

  if ((Sort & SORT_MASK) != SORT_THREADS)
    return (1);

  threads[0] = hdr->thread;
  while (threads[0]->parent)
    threads[0] = threads[0]->parent;
  while (threads[0]->prev)
    threads[0] = threads[0]->prev;

  if (flag)
    threads[1] = hdr->thread;
  else
    threads[1] = threads[0]->next;

  for (i = 0; i < flag ? 1 : 2; i++)
  {
    while (!threads[i]->message)
      threads[i] = threads[i]->child;
  } 

  return (((Sort & SORT_REVERSE ? -1 : 1)
	   * threads[1]->message->msgno - threads[0]->message->msgno) + (flag ? 1 : 0));
}

HASH *mutt_make_id_hash (CONTEXT *ctx)
{
  int i;
  HEADER *hdr;
  HASH *hash;

  hash = hash_create (ctx->msgcount * 2);

  for (i = 0; i < ctx->msgcount; i++)
  {
    hdr = ctx->hdrs[i];
    if (hdr->env->message_id)
      hash_insert (hash, hdr->env->message_id, hdr, 0);
  }

  return hash;
}

HASH *mutt_make_subj_hash (CONTEXT *ctx)
{
  int i;
  HEADER *hdr;
  HASH *hash;

  hash = hash_create (ctx->msgcount * 2);

  for (i = 0; i < ctx->msgcount; i++)
  {
    hdr = ctx->hdrs[i];
    if (hdr->env->real_subj)
      hash_insert (hash, hdr->env->real_subj, hdr, 1);
  }

  return hash;
}
