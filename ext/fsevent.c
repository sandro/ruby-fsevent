
#include "fsevent.h"

/*
 * Thanks to fswatch.c for providing a starting point
 * http://github.com/alandipert/fswatch
*/

void Init_cfrunloop _((void));

VALUE fsevent_class;
ID id_run_loop;
ID id_on_change;

// =========================================================================
// This method cleans up the resources used by our fsevent notifier. The run
// loop is stopped and released, and the notification stream is stopped and
// released. All our internal references are set to null.
static void
fsevent_struct_release( FSEvent fsevent )
{
  if (NULL == fsevent) return;

  if (fsevent->stream) {
    FSEventStreamStop(fsevent->stream);
    FSEventStreamInvalidate(fsevent->stream);
    FSEventStreamRelease(fsevent->stream);
  }
  fsevent->stream = 0;
}

// This method is called by the ruby garbage collector during the sweep phase
// of the garbage collection cycle. Memory allocated from the heap is freed
// and resources are released back to the mach kernel.
static void
fsevent_struct_free( void* ptr ) {
  if (NULL == ptr) return;

  FSEvent fsevent = (FSEvent) ptr;
  fsevent_struct_release(fsevent);

  xfree(fsevent);
}

// This method is called by the ruby interpreter when a new fsevent instance
// needs to be created. Memory is allocated from the heap and initialized to
// null values. A pipe is created at allocation time and lives for the
// duration of the instance.
static VALUE
fsevent_struct_allocate( VALUE klass ) {
  FSEvent fsevent = ALLOC_N( struct FSEvent_Struct, 1 );
  fsevent->stream = 0;
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
  void* arg,
  size_t numEvents,
  void* eventPaths,
  const FSEventStreamEventFlags eventFlags[],
  const FSEventStreamEventId eventIds[]
) {
  VALUE self = (VALUE) arg;
  char** paths = eventPaths;
  int ii, io;
  long length = 0;

  io = cfrunloop_struct(rb_ivar_get(self, id_run_loop))->pipes[1];

  for (ii=0; ii<numEvents; ii++) { length += strlen(paths[ii]); }
  length += numEvents - 1;

  // send the object id and the length of the paths down the pipe
  write(io, (char*) &self, sizeof(VALUE));
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
static void
fsevent_start_stream( VALUE self ) {
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
  CFRunLoop cfrunloop = cfrunloop_struct(rb_ivar_get(self, id_run_loop));

  FSEventStreamContext context;
  context.version = 0;
  context.info = (void *) self;
  context.retain = NULL;
  context.release = NULL;
  context.copyDescription = NULL;

  fsevent->stream = FSEventStreamCreate(NULL,
    &fsevent_callback,
    &context,
    pathsToWatch,
    kFSEventStreamEventIdSinceNow,
    latency,
    kFSEventStreamCreateFlagNone
  );

  FSEventStreamScheduleWithRunLoop(fsevent->stream, cfrunloop->run_loop, kCFRunLoopDefaultMode);
  FSEventStreamStart(fsevent->stream);

  if (!cfrunloop->running) cfrunloop_signal(cfrunloop);
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

  VALUE rloop = rb_iv_get(cfrunloop_class, "@instance");
  if (!RTEST(rloop)) {
    rb_raise(rb_eRuntimeError, "could not obtain a run loop" );
  }
  rb_ivar_set(self, id_run_loop, rloop);

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

static VALUE
fsevent_rb_callback( VALUE self, VALUE string ) {
  VALUE ary = rb_str_split(string, "\n");
  rb_funcall2(self, id_on_change, 1, &ary);
  return self;
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
  if (fsevent->stream) return self;

  VALUE ary = rb_iv_get(self, "@directories");
  if (NIL_P(ary)) rb_raise(rb_eRuntimeError, "no directories to watch");
  Check_Type(ary, T_ARRAY);

  fsevent_start_stream(self);
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
  if (fsevent->stream) return Qtrue;
  return Qfalse;
}

// =========================================================================
void Init_fsevent() {
  id_run_loop = rb_intern("@run_loop");
  id_on_change = rb_intern("on_change");

  fsevent_class = rb_define_class( "FSEvent", rb_cObject );
  rb_define_alloc_func( fsevent_class, fsevent_struct_allocate );
  rb_define_method( fsevent_class, "initialize", fsevent_init, -1 );

  rb_define_attr( fsevent_class, "run_loop",    1, 0 );
  rb_define_attr( fsevent_class, "latency",     1, 1 );
  rb_define_attr( fsevent_class, "directories", 1, 0 );

  rb_define_method( fsevent_class, "watch",     fsevent_watch,       1 );
  rb_define_method( fsevent_class, "stop",      fsevent_stop,        0 );
  rb_define_method( fsevent_class, "start",     fsevent_start,       0 );
  rb_define_method( fsevent_class, "restart",   fsevent_restart,     0 );
  rb_define_method( fsevent_class, "running?",  fsevent_is_running,  0 );

  rb_define_private_method( fsevent_class, "_callback", fsevent_rb_callback, 1 );

  rb_define_alias( fsevent_class, "directories=",      "watch" );
  rb_define_alias( fsevent_class, "watch_directories", "watch" );

  // initialize other classes
  Init_cfrunloop();
}

