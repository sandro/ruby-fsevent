
#include <ruby.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <CoreServices/CoreServices.h>

/*
 * Thanks to fswatch.c for providing a starting point
 * http://github.com/alandipert/fswatch
*/

VALUE fsevent_class;

struct FSEvent_Struct {
  int pipes[2];
  pthread_t thread;
  FSEventStreamRef stream;
  CFRunLoopRef run_loop;
};
typedef struct FSEvent_Struct* FSEvent;

// =========================================================================
// This method cleans up the resources used by our fsevent notifier. The run
// loop is stopped and released, and the notification stream is stopped and
// released. All our internal references are set to null.
static void
fsevent_struct_release( FSEvent fsevent )
{
  if (NULL == fsevent) return;

  if (fsevent->run_loop) {
    CFRunLoopStop(fsevent->run_loop);
    CFRelease(fsevent->run_loop);
  }
  fsevent->run_loop = 0;

  if (fsevent->stream) {
    FSEventStreamStop(fsevent->stream);
    FSEventStreamInvalidate(fsevent->stream);
    FSEventStreamRelease(fsevent->stream);
  }
  fsevent->stream = 0;

  fsevent->thread = 0;
}

// This method is called by the ruby garbage collector during the sweep phase
// of the garbage collection cycle. Memory allocated from the heap is freed
// and resources are released back to the mach kernel.
static void
fsevent_struct_free( void* ptr ) {
  if (NULL == ptr) return;

  FSEvent fsevent = (FSEvent) ptr;
  fsevent_struct_release(fsevent);

  if (fsevent->pipes[0]) {close(fsevent->pipes[0]);}
  if (fsevent->pipes[1]) {close(fsevent->pipes[1]);}
  fsevent->pipes[0] = 0;
  fsevent->pipes[1] = 0;

  xfree(fsevent);
}

// This method is called by the ruby interpreter when a new fsevent instance
// needs to be created. Memory is allocated from the heap and initialized to
// null values. A pipe is created at allocation time and lives for the
// duration of the instance.
static VALUE
fsevent_struct_allocate( VALUE klass ) {
  FSEvent fsevent = ALLOC_N( struct FSEvent_Struct, 1 );

  fsevent->thread = 0;
  fsevent->stream = 0;
  fsevent->run_loop = 0;

  if (pipe(fsevent->pipes) == -1) { rb_sys_fail(0); }

  return Data_Wrap_Struct( klass, NULL, fsevent_struct_free, fsevent );
}

// A helper method used to extract the fsevent C struct from the ruby "self" VALUE.
static FSEvent
fsevent_struct( VALUE self ) {
  FSEvent fsevent;

  if (TYPE(self) != T_DATA
  ||  RDATA(self)->dfree != (RUBY_DATA_FUNC) fsevent_struct_free) {
    rb_raise(rb_eTypeError, "expecting an FSEvent object");
  }
  Data_Get_Struct( self, struct FSEvent_Struct, fsevent );
  return fsevent;
}

// This method is called by the mach kernel to notify our instance that a
// filesystem change has occurred in one of the directories we are watching.
//
// The reference to our FSEvent ruby object is passed in as the second
// parameter. We extract the underlying fsevent struct from the ruby object
// and grab the write end of the pipe. The "eventPaths" we received from the
// kernel are written to this pipe as a single string with a newline "\n"
// character between each path.
//
// The user will call the "changes" method on the ruby oejct in order to read
// these paths out from the pipe when needed. The use of the pipe allows 1)
// the callback to return quickly to the kernel, and 2) for notifications to
// be queued in the pipe so they are not missed by the main ruby thread.
static void
fsevent_callback(
  ConstFSEventStreamRef streamRef,
  void* self,
  size_t numEvents,
  void* eventPaths,
  const FSEventStreamEventFlags eventFlags[],
  const FSEventStreamEventId eventIds[]
) {
  char** paths = eventPaths;
  FSEvent fsevent = fsevent_struct((VALUE) self);
  int ii, io;
  long length = 0;

  io = fsevent->pipes[1];

  for (ii=0; ii<numEvents; ii++) { length += strlen(paths[ii]); }
  length += numEvents - 1;

  // send the length of the paths down the pipe
  write(io, (char*) &length, sizeof(long));

  // send each path down the pipe separated by newlines
  for (ii=0; ii<numEvents; ii++) {
    if (ii > 0) { write(io, "\n", 1); }
    write(io, paths[ii], strlen(paths[ii]));
  }
}

// Start execution of the Mac CoreFramework run loop. This method is spawned
// inside a pthread so that the ruby interpreter is not blocked (the
// CFRunLoopRun method never returns).
static void*
fsevent_start_run_loop( void* _self ) {
  VALUE self = (VALUE) _self;

  // ignore all signals in this thread
  sigset_t all_signals;
  sigfillset(&all_signals);
  pthread_sigmask(SIG_BLOCK, &all_signals, 0);

  FSEvent fsevent = fsevent_struct(self);

  // convert our registered directory array into an OS X array of references
  VALUE ary = rb_iv_get(self, "@directories");
  int ii, length;
  length = RARRAY_LEN(ary);

  CFStringRef paths[length];
  for (ii=0; ii<length; ii++) {
    paths[ii] =
        CFStringCreateWithCString(
            NULL,
            (char*) RSTRING_PTR(RARRAY_PTR(ary)[ii]),
            kCFStringEncodingUTF8
        );
  }

  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **) &paths, length, NULL);
  CFAbsoluteTime latency = NUM2DBL(rb_iv_get(self, "@latency"));

  FSEventStreamContext context;
  context.version = 0;
  context.info = (void *) self;
  context.retain = NULL;
  context.release = NULL;
  context.copyDescription = NULL;

  fsevent->run_loop = CFRunLoopGetCurrent();
  CFRetain(fsevent->run_loop);

  fsevent->stream = FSEventStreamCreate(NULL,
    &fsevent_callback,
    &context,
    pathsToWatch,
    kFSEventStreamEventIdSinceNow,
    latency,
    kFSEventStreamCreateFlagNone
  );

  FSEventStreamScheduleWithRunLoop(fsevent->stream, fsevent->run_loop, kCFRunLoopDefaultMode);
  FSEventStreamStart(fsevent->stream);
  CFRunLoopRun();
  pthread_exit(NULL);
}

static VALUE fsevent_watch(VALUE, VALUE);


// FSEvent ruby methods start here

// =========================================================================
/* call-seq:
 *    FSEvent.new( )
 *    FSEvent.new( directories )
 *    FSEvent.new( latency )
 *    FSEvent.new( directories, latency )
 *
 * Create a new FSEvent notifier instance configured to recieve notifications
 * for the given _directories_ with a specific _latency_. After the notifier
 * is created, you must call the +start+ method to begin receiving
 * notifications.
 *
 * The _directories_ can be either a single path or an array of paths. Each
 * directory and all its sub-directories will be monitored for file system
 * modifications. The exact directory where the event occurred will be passed
 * to the user when the +changes+ method is called.
 *
 * Clients can supply a _latency_ parameter that tells how long to wait after
 * an event occurs before notifying; this reduces the volume of events and
 * reduces the chance that the client will see an "intermediate" state.
 */
static VALUE
fsevent_init( int argc, VALUE* argv, VALUE self ) {
  rb_iv_set(self, "@latency", rb_float_new(0.5));
  rb_iv_set(self, "@directories", Qnil);

  VALUE tmp1, tmp2;
  switch (rb_scan_args( argc, argv, "02", &tmp1, &tmp2 )) {
  case 1:
    if (TYPE(tmp1) == T_FLOAT || TYPE(tmp1) == T_FIXNUM) rb_iv_set(self, "@latency", tmp1);
    else fsevent_watch(self, tmp1);
    break;
  case 2:
    fsevent_watch(self, tmp1);
    if (TYPE(tmp2) == T_FLOAT || TYPE(tmp2) == T_FIXNUM) rb_iv_set(self, "@latency", tmp2);
    else rb_raise(rb_eTypeError, "latency must be a Numeric value" );
    break;
  }
  return self;
}

/* call-seq:
 *    changes( timeout = nil )
 *
 * Request the list of directory paths that have changed. This method will
 * block until one of the monitoried directories has changed or until
 * _timeout_ seconds have elapsed.
 *
 * Returns an array containing the directory paths where a file system event
 * has occurred. If the _timeout_ is reached before any events occur then
 * +nil+ is returned.
 */
static VALUE
fsevent_changes( int argc, VALUE* argv, VALUE self ) {
  VALUE tmp = Qnil;
  struct timeval tv;
  struct timeval *tv_ptr = 0;

  rb_scan_args( argc, argv, "01", &tmp );
  if (!NIL_P(tmp)) {
    double timeout = NUM2DBL(tmp);
    tv.tv_sec = (int) floor(timeout);
    tv.tv_usec = (int) ((timeout - tv.tv_sec) * 1E6);
    tv_ptr = &tv;
  }

  fd_set set;
  int io = fsevent_struct(self)->pipes[0];
  FD_SET(io, &set);
  int rv = rb_thread_select(io+1, &set, NULL, NULL, tv_ptr);

  if (rv < 0) rb_sys_fail("could not receive events from pipe");
  if (0 == rv) return Qnil;

  // messages are available
  VALUE ary = Qnil;
  VALUE string = rb_str_buf_new(0);
  long length = 0;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  // drain all messages from the pipe
  while (rb_thread_select(io+1, &set, NULL, NULL, &tv) > 0) {
    // read the message length from the pipe
    read(io, &length, sizeof(long));
    rb_str_resize(string, length);

    // read the message from the pipe
    read(io, RSTRING_PTR(string), length);

    // split the string and concatenate the resuls to our return array
    if (NIL_P(ary)) ary = rb_str_split(string, "\n");
    else rb_ary_concat(ary, rb_str_split(string, "\n"));
  }

  return ary;
}

/* call-seq:
 *    changes?
 *
 * This method will return +true+ if the are file system events ready to be
 * processed; when this method returns +true+, the FSEvent#changes method is
 * guaranteed not to block.
 *
 * This method will return +false+ if there are no file system events
 * available.
 */
static VALUE
fsevent_has_changes( VALUE self ) {
  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  fd_set set;
  int io = fsevent_struct(self)->pipes[0];
  FD_SET(io, &set);
  int rv = rb_thread_select(io+1, &set, NULL, NULL, &tv);

  if (rv < 0) rb_sys_fail("could not receive events from pipe");
  if (0 == rv) return Qfalse;

  return Qtrue;
}

/* call-seq:
 *    directories = ['paths']
 *    watch( directories )
 *
 * Set a single directory or an array of directories to be monitored by this
 * fsevent notifier.
 */
static VALUE
fsevent_watch( VALUE self, VALUE directories ) {
  switch (TYPE(directories)) {
  case T_ARRAY:
    rb_iv_set(self, "@directories", rb_ary_new4(RARRAY_LEN(directories), RARRAY_PTR(directories)));
    break;
  case T_STRING:
    rb_iv_set(self, "@directories", rb_ary_new3(1, directories));
    break;
  case T_NIL:
    rb_iv_set(self, "@directories", Qnil);
    break;
  default:
    rb_raise(rb_eTypeError,
          "directories must be given as a String or an Array of strings");
  }
  return rb_iv_get(self, "@directories");
}

/* call-seq:
 *    start
 *
 * Start the event notification thread and begin receiving file system events
 * from the operating system. Calling this method multiple times will have no
 * ill affects. Returns the FSEvent notifier instance.
 */
static VALUE
fsevent_start( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  if (fsevent->thread) return self;

  VALUE ary = rb_iv_get(self, "@directories");
  if (NIL_P(ary)) rb_raise(rb_eRuntimeError, "no directories to watch");
  Check_Type(ary, T_ARRAY);

  int rv = pthread_create(&fsevent->thread, NULL, fsevent_start_run_loop, (void*) self);
  if (0 != rv) rb_raise(rb_eRuntimeError, "could not start FSEvent thread");

  return self;
}

/* call-seq:
 *    stop
 *
 * Stop the event notification thread. Calling this method multiple times will
 * have no ill affects. Returns the FSEvent notifier instance.
 */
static VALUE
fsevent_stop( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  fsevent_struct_release(fsevent);
  return self;
}

/* call-seq:
 *    restart
 *
 * Stop the event notification thread if running and then start it again. Any
 * changes to the directories to watch or the latency will be picked up when
 * the FSEvent notifier is restarted. Returns the FSEvent notifier instance.
 */
static VALUE
fsevent_restart( VALUE self ) {
  fsevent_stop(self);
  fsevent_start(self);
  return self;
}

/* call-seq:
 *    running?
 *
 * Returns +true+ if the notification thread is running. Returns +false+ if
 * this is not the case.
 */
static VALUE
fsevent_is_running( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  if (fsevent->thread) return Qtrue;
  return Qfalse;
}

// =========================================================================
void Init_fsevent() {
  fsevent_class = rb_define_class( "FSEvent", rb_cObject );
  rb_define_alloc_func( fsevent_class, fsevent_struct_allocate );
  rb_define_method( fsevent_class, "initialize", fsevent_init, -1 );

  rb_define_attr( fsevent_class, "latency",     1, 1 );
  rb_define_attr( fsevent_class, "directories", 1, 0 );

  rb_define_method( fsevent_class, "watch",     fsevent_watch,        1 );
  rb_define_method( fsevent_class, "changes",   fsevent_changes,     -1 );
  rb_define_method( fsevent_class, "changes?",  fsevent_has_changes,  0 );
  rb_define_method( fsevent_class, "stop",      fsevent_stop,         0 );
  rb_define_method( fsevent_class, "start",     fsevent_start,        0 );
  rb_define_method( fsevent_class, "restart",   fsevent_restart,      0 );
  rb_define_method( fsevent_class, "running?",  fsevent_is_running,   0 );

  rb_define_alias( fsevent_class, "directories=",          "watch"    );
  rb_define_alias( fsevent_class, "watch_directories",     "watch"    );
  rb_define_alias( fsevent_class, "changed_directories",   "changes"  );
  rb_define_alias( fsevent_class, "changed_directories?",  "changes?" );
}

