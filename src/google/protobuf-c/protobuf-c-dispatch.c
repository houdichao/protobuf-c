#include "protobuf-c-dispatch.h"
#include "gskrbtreemacros.h"

#define ALLOC_WITH_ALLOCATOR(allocator, size) ((allocator)->alloc ((allocator)->allocator_data, (size)))
#define FREE_WITH_ALLOCATOR(allocator, ptr)   ((allocator)->free ((allocator)->allocator_data, (ptr)))
#define ALLOC(size)   ALLOC_WITH_ALLOCATOR(&protobuf_c_system_allocator, size)
#define FREE(ptr)     FREE_WITH_ALLOCATOR(&protobuf_c_system_allocator, ptr)

typedef struct _Callback Callback;
struct _Callback
{
  ProtobufCDispatchCallback func;
  void *data;
};

typedef struct _FDMap FDMap;
struct _FDMap
{
  int notify_desired_index;     /* -1 if not an known fd */
  int change_index;             /* -1 if no prior change */
  int closed_since_notify_started;
};

typedef struct _RealDispatch RealDispatch;
struct _RealDispatch
{
  ProtobufCDispatch base;
  Callback *callbacks;          /* parallels notifies_desired */
  size_t notifies_desired_alloced;
  size_t changes_alloced;
  FDMap *fd_map;                /* map indexed by fd */
  size_t fd_map_size;           /* number of elements of fd_map */

  ProtobufCDispatchTimer *timer_tree;
};

struct _ProtobufCDispatchTimer
{
  /* the actual timeout time */
  unsigned long timeout_secs;
  unsigned timeout_usecs;

  /* red-black tree stuff */
  ProtobufCDispatchTimer *left, *right, *parent;
  protobuf_c_boolean is_red;

  /* user callback */
  ProtobufCDispatchTimerFunc func;
  void *func_data;
};

/* Define the tree of timers, as per gskrbtreemacros.h */
#define TIMER_GET_IS_RED(n)      ((n)->is_red)
#define TIMER_SET_IS_RED(n,v)    ((n)->is_red = (v))
#define TIMERS_COMPARE(a,b, rv) \
  if (a->timeout_secs < b->timeout_secs) rv = -1; \
  else if (a->timeout_secs > b->timeout_secs) rv = 1; \
  else if (a->timeout_usecs < b->timeout_usecs) rv = -1; \
  else if (a->timeout_usecs > b->timeout_usecs) rv = 1; \
  else if (a < b) rv = -1; \
  else if (a > b) rv = 1; \
  else rv = 0;
#define GET_TIMER_TREE(d) \
  (d)->timer_tree, ProtobufCDispatchTimer *, \
  TIMER_GET_IS_RED, TIMER_SET_IS_RED, \
  parent, left, right, \
  TIMERS_COMPARE

/* Create or destroy a Dispatch */
ProtobufCDispatch *protobuf_c_dispatch_new (void)
{
  RealDispatch *rv = ALLOC (sizeof (RealDispatch));
  rv->base.n_changes = 0;
  rv->notifies_desired_alloced = 8;
  rv->base.notifies_desired = ALLOC (sizeof (ProtobufC_FDNotify) * rv->notifies_desired_alloced);
  rv->callbacks = ALLOC (sizeof (Callback) * rv->notifies_desired_alloced);
  rv->changes_alloced = 8;
  rv->base.changes = ALLOC (sizeof (ProtobufC_FDNotify) * rv->changes_alloced);
  rv->fd_map_size = 16;
  rv->fd_map = ALLOC (sizeof (FDMap) * rv->fd_map_size);
  memset (rv->fd_map, 255, sizeof (FDMap) * rv->fd_map_size);
  return &rv->base;
}

void
protobuf_c_dispatch_free(ProtobufCDispatch *dispatch)
{
  RealDispatch *d = (RealDispatch *) dispatch;
  FREE (d->base.notifies_desired);
  FREE (d->callbacks);
  FREE (d->fd_map);
  FREE (d);
}

static void
enlarge_fd_map (RealDispatch *d,
                unsigned      fd)
{
  size_t new_size = d->fd_map_size * 2;
  FDMap *new_map;
  while (fd >= new_size)
    new_size *= 2;
  new_map = ALLOC (sizeof (FDMap) * new_size);
  memcpy (new_map, d->fd_map, d->fd_map_size * sizeof (FDMap));
  memset (new_map + d->fd_map_size,
          255,
          sizeof (FDMap) * (new_size - d->fd_map_size));
  FREE (d->fd_map);
  d->fd_map = new_map;
  d->fd_map_size = new_size;
}

static inline void
ensure_fd_map_big_enough (RealDispatch *d,
                          unsigned      fd)
{
  if (fd >= d->fd_map_size)
    enlarge_fd_map (d, fd);
}

static unsigned
allocate_notifies_desired_index (RealDispatch *d)
{
  unsigned rv = d->base.n_notifies_desired++;
  if (rv == d->notifies_desired_alloced)
    {
      unsigned new_size = d->notifies_desired_alloced * 2;
      ProtobufC_FDNotify *n = ALLOC (new_size * sizeof (ProtobufC_FDNotify));
      memcpy (n, d->base.notifies_desired, d->notifies_desired_alloced * sizeof (ProtobufC_FDNotify));
      FREE (d->base.notifies_desired);
      d->base.notifies_desired = n;
      d->notifies_desired_alloced = new_size;
    }
  return rv;
}
static unsigned
allocate_change_index (RealDispatch *d)
{
  unsigned rv = d->base.n_changes++;
  if (rv == d->changes_alloced)
    {
      unsigned new_size = d->changes_alloced * 2;
      ProtobufC_FDNotify *n = ALLOC (new_size * sizeof (ProtobufC_FDNotify));
      memcpy (n, d->base.changes, d->changes_alloced * sizeof (ProtobufC_FDNotify));
      FREE (d->base.changes);
      d->base.changes = n;
      d->changes_alloced = new_size;
    }
  return rv;
}
static void
deallocate_change_index (RealDispatch *d,
                         int fd)
{
  unsigned ch_ind = d->fd_map[fd].change_index;
  unsigned from = d->n_changes - 1;
  unsigned from_fd;
  if (ch_ind == from)
    {
      d->n_changes--;
      return;
    }
  from_fd = d->base.changes[ch_ind].fd;
  d->fd_map[from_fd].change_index = ch_ind;
  d->changes[ch_ind] = d->changes[from];
  d->n_changes--;
}

static void
deallocate_notify_desired_index (RealDispatch *d,
                                 int fd)
{
  unsigned nd_ind = d->fd_map[fd].notify_desired_index;
  unsigned from = d->n_notifies_desired - 1;
  unsigned from_fd;
  if (nd_ind == from)
    {
      d->n_notifies_desired--;
      return;
    }
  from_fd = d->base.notifies_desired[nd_ind].fd;
  d->fd_map[from_fd].notify_desired_index = nd_ind;
  d->notifies_desired[nd_ind] = d->notifies_desired[from];
  d->n_notifies_desired--;
}


/* Registering file-descriptors to watch. */
void
protobuf_c_dispatch_watch_fd (ProtobufCDispatch *dispatch,
                              int                 fd,
                              unsigned            events,
                              ProtobufCDispatchCallback callback,
                              void               *callback_data)
{
  RealDispatch *d = (RealDispatch *) dispatch;
  unsigned f = fd;              /* avoid tiring compiler warnings: "comparison of signed versus unsigned" */
  guint nd_ind, change_ind;
  if (callback == NULL)
    assert (events == 0);
  else
    assert (events != 0);
  ensure_fd_map_big_enough (d, f);
  if (d->fd_map[f].notify_desired_index == -1)
    {
      d->fd_map[f].notify_desired_index = allocate_notifies_desired_index (d);
    }
  else
    {
      if (callback == NULL)
        deallocate_notify_desired_index (d, f);
      nd_ind = d->fd_map[f].notify_desired_index;
    }
  if (callback == NULL)
    {
      if (d->fd_map[f].change_index == -1)
        d->fd_map[f].change_index = allocate_change_index (d);
      change_ind = d->fd_map[f].change_index;
      d->base.changes[change_ind].fd = f;
      d->base.changes[change_ind].events = 0;
      return;
    }
  assert (callback != NULL && events != 0);
  if (d->fd_map[f].change_index == -1)
    d->fd_map[f].change_index = allocate_change_index (d);
  change_ind = d->fd_map[f].change_index;

  d->base.changes[ch_ind].fd = fd;
  d->base.changes[ch_ind].events = events;
  d->base.notifies_desired[nd_ind].fd = fd;
  d->base.notifies_desired[nd_ind].events = events;
  d->callbacks[nd_ind].func = callback;
  d->callbacks[nd_ind].data = callback_data;
}

void
protobuf_c_dispatch_close_fd (ProtobufCDispatch *dispatch,
                              int                 fd)
{
  protobuf_c_dispatch_fd_closed (dispatch, fd);
  close (fd);
}

void
protobuf_c_dispatch_fd_closed(ProtobufCDispatch *dispatch,
                              int                 fd)
{
  unsigned f = fd;
  RealDispatch *d = (RealDispatch *) dispatch;
  ensure_fd_map_big_enough (d, f);
  d->fd_map[fd].closed_since_notify_started = 1;
  if (d->fd_map[f].change_index != -1)
    deallocate_change_index (d, f);
  if (d->fd_map[f].notify_desired_index != -1)
    deallocate_notify_desired_index (d, f);
}

static void
free_timer (ProtobufCDispatchTimer *timer)
{
  RealDispatch *d = timer->dispatch;
  timer->right = d->recycled_timeouts;
  d->recycled_timeouts = timer;
}

void
protobuf_c_dispatch_dispatch (ProtobufCDispatch *dispatch,
                              size_t              n_notifies,
                              ProtobufC_FDNotify *notifies)
{
  RealDispatch *d = (RealDispatch *) dispatch;
  unsigned fd_max;
  unsigned i;
  if (n_notifies == 0)
    return;
  fd_max = 0;
  for (i = 0; i < n_notifies; i++)
    if (fd_max < (unsigned) notifies[i].fd)
      fd_max = notifies[i].fd;
  ensure_fd_map_big_enough (d, fd_max);
  for (i = 0; i < n_notifies; i++)
    fd_map[notifies[i].fd].closed_since_notify_started = 0;
  for (i = 0; i < n_notifies; i++)
    {
      unsigned fd = notifies[i].fd;
      if (!fd_map[fd].closed_since_notify_started
       && fd_map[fd].notify_desired_index != -1)
        {
          unsigned nd_ind = fd_map[fd].notify_desired_index;
          unsigned events = d->base.notifies_desired[nd_ind].events & notifies[i].events;
          if (events != 0)
            d->callbacks[nd_ind].func (fd, events, d->callbacks[nd_ind].data);
        }
    }


  /* handle timers */
  gettimeofday (&tv, NULL);
  while (d->timer_tree != NULL)
    {
      ProtobufCDispatchTimer *min_timer;
      GSK_RBTREE_FIRST (GET_TIMER_TREE (d), min_timer);
      if (min_timer.timeout_secs < tv.tv_secs
       || (min_timer.timeout_secs == tv.tv_secs
        && min_timer.timeout_usecs <= tv.tv_usecs))
        {
          ProtobufCDispatchTimerFunc func = min_timer->func;
          void *func_data = min_timer->func_data;
          GSK_RBTREE_REMOVE (GET_TIMER_TREE (d), min_timer);
          /* Set to NULL as a way to tell protobuf_c_dispatch_remove_timer()
             that we are in the middle of notifying */
          min_timer->func = NULL;
          min_timer->func_data = NULL;
          func (&d->base, func_data);
          free_timer (min_timer);
        }
      else
        {
          d->base.has_timeout = 1;
          d->base.timeout_secs = min_timer->timeout_secs;
          d->base.timeout_usecs = min_timer->timeout_usecs;
          break;
        }
    }
  if (d->timer_tree == NULL)
    d->base.has_timeout = 0;
}

void
protobuf_c_dispatch_clear_changes (ProtobufCDispatch *dispatch)
{
  RealDispatch *d = (RealDispatch *) dispatch;
  unsigned i;
  for (i = 0; i < dispatch->n_changes; i++)
    {
      assert (fd_map[dispatch->changes[i].fd].change_index == (int) i);
      fd_map[dispatch->changes[i].fd].change_index = -1;
    }
  dispatch->n_changes = 0;
}

void
protobuf_c_dispatch_run (ProtobufCDispatch *dispatch)
{
  struct pollfd *fds;
  void *to_free = NULL;
  size_t n_events;
  if (dispatch->n_notifies_desired < 128)
    fds = alloca (sizeof (struct pollfd) * dispatch->n_notifies_desired);
  else
    to_free = fds = ALLOC (sizeof (struct pollfd) * dispatch->n_notifies_desired);
  for (i = 0; i < dispatch->n_notifies_desired; i++)
    {
      fds[i].fd = dispatch->notifies_desired[i].fd;
      fds[i].events = events_to_pollfd_events (dispatch->notifies_desired[i].events);
      fds[i].revents = 0;
    }

  if (poll (fds, dispatch->n_notifies_desired, timeout) < 0)
    {
      if (errno == EINTR)
        return;                 /* probably a signal interrupted the poll-- let the user have control */

      /* i don't really know what would plausibly cause this */
      fprintf (stderr, "error polling: %s\n", strerror (errno));
      return;
    }
  n_events = 0;
  for (i = 0; i < dispatch->n_notifies_desired; i++)
    if (fds[i].revents)
      n_events++;
  if (n_events < 128)
    to_free2 = events = alloca (sizeof (ProtobufC_FDNotify) * n_events);
  else
    events = ALLOC (sizeof (ProtobufC_FDNotify) * n_events);
  n_events = 0;
  for (i = 0; i < dispatch->n_notifies_desired; i++)
    if (fds[i].revents)
      {
        events[n_events].fd = fds[i].fd;
        events[n_events].events = pollfd_events_to_events (fds[i].revents);

        /* note that we may actually wind up with fewer events
           now that we actually call pollfd_events_to_events() */
        if (events[n_events].events != 0)
          n_events++;
      }
  protobuf_c_dispatch_clear_changes (dispatch);
  protobuf_c_dispatch_dispatch (dispatch, n_events, events);
  if (to_free)
    FREE (to_free);
  if (to_free2)
    FREE (to_free2);
}

ProtobufCDispatchTimer *
protobuf_c_dispatch_add_timer(ProtobufCDispatch *dispatch,
                              unsigned            timeout_secs,
                              unsigned            timeout_usecs,
                              ProtobufCDispatchTimerFunc func,
                              void               *func_data)
{
  RealDispatch *d = (RealDispatch *) dispatch;
  protobuf_c_assert (func != NULL);
  if (d->recycled_timeouts != NULL)
    {
      rv = d->recycled_timeouts;
      d->recycled_timeouts = rv->right;
    }
  else
    {
      rv = d->allocator->alloc (d->allocator, sizeof (ProtobufCDispatchTimer));
    }
  rv->timeout_secs = timeout_secs;
  rv->timeout_usecs = timeout_usecs;
  rv->func = func;
  rv->func_data = func_data;
  rv->dispatch = d;
  GSK_RBTREE_INSERT (GET_TIMER_TREE (d), rv, conflict);
  return rv;
}

void  protobuf_c_dispatch_remove_timer (ProtobufCDispatchTimer *timer)
{
  protobuf_c_boolean may_be_first;
  RealDispatch *d = timer->dispatch;

  /* ignore mid-notify removal */
  if (timer->func == NULL)
    return;

  may_be_first = d->base.timeout_usecs == timer->timeout_usecs
              && d->base.timeout_secs == timer->timeout_secs;

  GSK_RBTREE_REMOVE (GET_TIMER_TREE (d), timer);

  if (may_be_first)
    {
      if (d->timer_tree == NULL)
        d->base.has_timeout = 0;
      else
        {
          ProtobufCDispatchTimer *min;
          GSK_RBTREE_FIRST (GET_TIMER_TREE (d), min);
          d->timeout_secs = min->timeout_secs;
          d->timeout_usecs = min->timeout_usecs;
        }
    }
}
