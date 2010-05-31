
#include "fsevent.h"

/*
 * Thanks to fswatch.c for providing a starting point
 * http://github.com/alandipert/fswatch
*/

VALUE cfrunloop_class;

// =========================================================================
// Signal any pthreads that are waiting on the conditional mutex. The various
// ruby incarnations of runloop objects (timers, fsevents) call this method
// in order to start the CFRunLoopRun method without blocking the ruby
// interpreter.
void
cfrunloop_signal( CFRunLoop cfrunloop ) {
  pthread_mutex_lock(&cfrunloop->mutex);
  pthread_cond_signal(&cfrunloop->cond);
  pthread_mutex_unlock(&cfrunloop->mutex);
}

// Causes the current pthread to wait on the conditional mutex. This method is
// called by our CFRunLoop object when it is waiting to enter the CFRunLoopRun
// method. It is called from within a pthread separate from the ruby
// interpreter. This prevents the interpreter from locking up inside the
// CFRunLoopRun method (which never returns).
static void
cfrunloop_wait( CFRunLoop cfrunloop ) {
  pthread_mutex_lock(&cfrunloop->mutex);
  pthread_cond_wait(&cfrunloop->cond, &cfrunloop->mutex);
  pthread_mutex_unlock(&cfrunloop->mutex);
}

// This method is called by the ruby garbage collector during the sweep phase
// of the garbage collection cycle. Memory allocated from the heap is freed
// and resources are released back to the mach kernel.
static void
cfrunloop_struct_free( void* ptr ) {
  if (NULL == ptr) return;

  CFRunLoop cfrunloop = (CFRunLoop) ptr;

  cfrunloop->thread = 0;
  if (cfrunloop->run_loop) {
    CFRunLoopStop(cfrunloop->run_loop);
    CFRelease(cfrunloop->run_loop);
  }
  cfrunloop->run_loop = 0;

  cfrunloop_signal(cfrunloop);
  pthread_mutex_destroy(&cfrunloop->mutex);
  pthread_cond_destroy(&cfrunloop->cond);

  if (cfrunloop->pipes[0]) {close(cfrunloop->pipes[0]);}
  if (cfrunloop->pipes[1]) {close(cfrunloop->pipes[1]);}
  cfrunloop->pipes[0] = 0;
  cfrunloop->pipes[1] = 0;


  xfree(cfrunloop);
}

// This method is called by the ruby interpreter when a new cfrunloop instance
// needs to be created. Memory is allocated from the heap and initialized to
// null values.
static VALUE
cfrunloop_struct_allocate( VALUE klass ) {
  CFRunLoop cfrunloop = ALLOC_N( struct CFRunLoop_Struct, 1 );

  cfrunloop->thread = 0;
  cfrunloop->run_loop = 0;
  cfrunloop->running = false;
  pthread_mutex_init(&cfrunloop->mutex, NULL);
  pthread_cond_init(&cfrunloop->cond, NULL);
  if (pipe(cfrunloop->pipes) == -1) { rb_sys_fail(0); }

  return Data_Wrap_Struct( klass, NULL, cfrunloop_struct_free, cfrunloop );
}

// A helper method used to extract the cfrunloop C struct from the ruby "self" VALUE.
CFRunLoop
cfrunloop_struct( VALUE self ) {
  CFRunLoop cfrunloop;

  if (TYPE(self) != T_DATA
  ||  RDATA(self)->dfree != (RUBY_DATA_FUNC) cfrunloop_struct_free) {
    rb_raise(rb_eTypeError, "expecting a CFRunLoop object");
  }
  Data_Get_Struct( self, struct CFRunLoop_Struct, cfrunloop );
  return cfrunloop;
}

// Start the pthread that will eventually enter the CFRunLoopRun method. We
// use a separate pthread so that the ruby interpreter does not lock up
// indefinitely waiting for the CFRunLoopRun method to return.
static void*
cfrunloop_start_thread( void* self ) {
  CFRunLoop cfrunloop = (CFRunLoop) self;

  // ignore all signals in this thread
  sigset_t all_signals;
  sigfillset(&all_signals);
  pthread_sigmask(SIG_BLOCK, &all_signals, 0);

  // get a reference to to the current Core Framework run loop
  cfrunloop->run_loop = CFRunLoopGetCurrent();
  CFRetain(cfrunloop->run_loop);

  for (;;) {
    cfrunloop_wait(cfrunloop);
    if (!cfrunloop->thread) pthread_exit(NULL); // gracefully exit the thread

    cfrunloop->running = true;
    CFRunLoopRun();
    cfrunloop->running = false;
  }
}

// This method is used to create a ruby thread - the peer to the pthread
// created in the method above.
//
// This thread reads messages from the pipe maintained by the CFRunLoop
// object. Each message is dispatched to the event object (timer, fsevent)
// that requested the notification.
static VALUE
cfrunloop_dispatcher( void* self ) {
  fd_set set;
  int rv;
  int io = cfrunloop_struct((VALUE) self)->pipes[0];
  long length = 0;

  VALUE obj;
  VALUE msg = rb_str_buf_new(0);

  FD_SET(io, &set);

  for (;;) {
    rv = rb_thread_select(io+1, &set, NULL, NULL, NULL);
    if (rv < 0) rb_sys_fail("could not receive events from pipe");
    if (rv > 0) {
      // read the object id from the pipe
      read(io, &obj, sizeof(VALUE));

      // read the message length from the pipe
      read(io, &length, sizeof(long));

      // read the message from the pipe
      if (length > 0) {
        rb_str_resize(msg, length);
        read(io, RSTRING_PTR(msg), length);
        fsevent_rb_callback(obj, msg);
      } else {
        fsevent_rb_callback(obj, Qnil);
      }
    }
  }

  return (VALUE) self;
}


// =========================================================================
/* call-seq:
 *    CFRunLoop.new
 *
 * Creates a single CFRunLoop instance. Multiple invocations of the new method
 * will always return the same instance; this instance is also expased via the
 * class level CFRunLoop.instance method.
 */
static VALUE
cfrunloop_new( VALUE klass ) {
  VALUE self = rb_iv_get( klass, "@instance" );

  if (!RTEST(self)) {
    self = rb_class_new_instance(0, NULL, klass);
    rb_iv_set( klass, "@instance", self );
  }

  return self;
}

/* call-seq:
 *    CFRunLoop.new
 *
 * Creates a single CFRunLoop instance. Multiple invocations of the new method
 * will always return the same instance; this instance is also expased via the
 * class level CFRunLoop.instance method.
 */
static VALUE
cfrunloop_init( VALUE self ) {
  CFRunLoop cfrunloop = cfrunloop_struct(self);
  int rv = pthread_create(&cfrunloop->thread, NULL, cfrunloop_start_thread, (void*) cfrunloop);
  if (0 != rv) rb_raise(rb_eRuntimeError, "could not start CFRunLoop thread");

  // wait for the run loop to be created
  for (;;) {
    if (cfrunloop->run_loop) break;
    rb_thread_schedule();
  }

  VALUE th = rb_thread_create( cfrunloop_dispatcher, (void*) self );
  rb_iv_set( self, "@thread", th );

  return self;
}

/* call-seq:
 *    start
 *
 * Start the internal run loop by invoking the CFRunLoopRun method inside a
 * separate pthread. This method will return immediately although it will take
 * a few cycles for the actual CFRunLoopRun method to be invoked.
 *
 * If the run loop has already been started, then this method returns without
 * taking any action.
 */
static VALUE
cfrunloop_start( VALUE self ) {
  CFRunLoop cfrunloop = cfrunloop_struct(self);
  if (!cfrunloop->running) cfrunloop_signal(cfrunloop);
  return self;
}

/* call-seq:
 *    stop
 *
 * Stop the interal run loop by invoking the CFRunLoopStop method. If the run
 * loop has already been stopped, then this method returns without taking any
 * action.
 */
static VALUE
cfrunloop_stop( VALUE self ) {
  CFRunLoop cfrunloop = cfrunloop_struct(self);
  if (cfrunloop->running) CFRunLoopStop(cfrunloop->run_loop);
  return self;
}

/* call-seq:
 *    running?
 *
 * Returns +true+ if in the internal run loop is running. Returns +false+
 * otherwise.
 */
static VALUE
cfrunloop_is_running( VALUE self ) {
  CFRunLoop cfrunloop = cfrunloop_struct(self);
  if (cfrunloop->running) return Qtrue;
  return Qfalse;
}

// =========================================================================
void Init_cfrunloop() {
  cfrunloop_class = rb_define_class( "CFRunLoop", rb_cObject );

  rb_define_alloc_func( cfrunloop_class, cfrunloop_struct_allocate );
  rb_define_singleton_method( cfrunloop_class, "new", cfrunloop_new, 0 );
  rb_define_method( cfrunloop_class, "initialize", cfrunloop_init, 0 );

  rb_define_method( cfrunloop_class, "stop",      cfrunloop_stop,        0 );
  rb_define_method( cfrunloop_class, "start",     cfrunloop_start,       0 );
  rb_define_method( cfrunloop_class, "running?",  cfrunloop_is_running,  0 );

  rb_define_attr( rb_singleton_class(cfrunloop_class), "instance", 1, 0 );
  cfrunloop_new( cfrunloop_class );
}

