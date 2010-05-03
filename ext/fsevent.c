
#include <ruby.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
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

static VALUE
fsevent_struct_allocate( VALUE klass ) {
  FSEvent fsevent = ALLOC_N( struct FSEvent_Struct, 1 );

  fsevent->thread = 0;
  fsevent->stream = 0;
  fsevent->run_loop = 0;

  if (pipe(fsevent->pipes) == -1) { rb_sys_fail(0); }

  return Data_Wrap_Struct( klass, NULL, fsevent_struct_free, fsevent );
}

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

static void
fsevent_start_run_loop( VALUE self ) {
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

// FIXME: accept mutiple optional args (directories and latency)
static VALUE
fsevent_init( VALUE self ) {
  rb_iv_set(self, "@directories", Qnil);
  rb_iv_set(self, "@latency", rb_float_new(0.5));
  return self;
}

// FIXME: needs to accept an optional timeout
static VALUE
fsevent_changes( VALUE self ) {
  double latency = NUM2DBL(rb_iv_get(self, "@latency"));
  FSEvent fsevent = fsevent_struct(self);
  fd_set set;
  struct timeval timeout;
  int io, rv;
  long length = 0;
  VALUE string = Qundef;

  io = fsevent->pipes[0];
  FD_SET(io, &set);

  // put call to select here
  timeout.tv_sec = (int) floor(latency);
  timeout.tv_usec = (int) ((latency - timeout.tv_sec) * 1E6);
  rv = select(io+1, &set, NULL, NULL, &timeout);

  if (rv < 0) rb_sys_fail("could not receive events from pipe");
  if (0 == rv) return Qnil;

  // get the size of the data passed into the pipe
  read(io, &length, sizeof(long));

  // read the data from the pipe
  string = rb_str_buf_new(length);
  read(io, RSTRING_PTR(string), length);

  RSTRING(string)->len = length;
  RSTRING(string)->ptr[length] = '\0';

  // FIXME: ifdef for switching between 1.8 and 1.9 strings
  //RSTRING(str)->as.heap.len = length;
  //RSTRING(str)->as.heap.ptr[length] = '\0';

  // split the string into an array
  return rb_str_split(string, "\n");
}

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

static VALUE
fsevent_stop( VALUE self ) {
  FSEvent fsevent = fsevent_struct(self);
  fsevent_struct_release(fsevent);
  return self;
}

static VALUE
fsevent_restart( VALUE self ) {
  fsevent_stop(self);
  fsevent_start(self);
  return self;
}

void Init_fsevent() {

  fsevent_class = rb_define_class("FSEvent", rb_cObject);
  rb_define_alloc_func( fsevent_class, fsevent_struct_allocate );
  rb_define_method( fsevent_class, "initialize", fsevent_init, 0);


  rb_define_attr( fsevent_class, "latency",     1, 1 );
  rb_define_attr( fsevent_class, "directories", 1, 0 );

  // changed_directories? changes?
  // running?
  // changes(timeout = @latency)
  rb_define_method( fsevent_class, "watch",    fsevent_watch,   1 );
  rb_define_method( fsevent_class, "changes",  fsevent_changes, 0 );
  rb_define_method( fsevent_class, "stop",     fsevent_stop,    0 );
  rb_define_method( fsevent_class, "start",    fsevent_start,   0 );
  rb_define_method( fsevent_class, "restart",  fsevent_restart, 0 );

  rb_define_alias( fsevent_class, "directories=",        "watch"   );
  rb_define_alias( fsevent_class, "watch_directories",   "watch"   );
  rb_define_alias( fsevent_class, "changed_directories", "changes" );
}

