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

FSEventStreamRef stream;
VALUE fsevent_class;

void callback(
  ConstFSEventStreamRef streamRef,
  void *context,
  size_t numEvents,
  void *eventPaths,
  const FSEventStreamEventFlags eventFlags[],
  const FSEventStreamEventId eventIds[]
) {
  VALUE self = (VALUE)context;
  int i;
  char **paths = eventPaths;
  VALUE rb_paths[numEvents];
  for (i = 0; i < numEvents; i++) {
    VALUE name = rb_str_new2(paths[i]);
    rb_paths[i] = name;
  }
  rb_funcall(self, rb_intern("on_change"), 1, rb_ary_new4(numEvents, rb_paths));
}

void watch_directory(VALUE self) {
  VALUE rb_registered_directories = rb_iv_get(self, "@registered_directories");
  int i, dir_size;
  dir_size = RARRAY_LEN(rb_registered_directories);

  VALUE *rb_dir_names = RARRAY_PTR(rb_registered_directories);
  CFStringRef dir_names[dir_size];
  for (i = 0; i < dir_size; i++) {
    dir_names[i] = CFStringCreateWithCString(NULL, (char *)RSTRING_PTR(rb_dir_names[i]), kCFStringEncodingUTF8);
  }

  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&dir_names, dir_size, NULL);


  VALUE rb_latency = rb_iv_get(self, "@latency");
  CFAbsoluteTime latency = NUM2DBL(rb_latency);

  FSEventStreamContext context;
  context.version = 0;
  context.info = (VALUE *)self;
  context.retain = NULL;
  context.release = NULL;
  context.copyDescription = NULL;

  stream = FSEventStreamCreate(NULL,
    &callback,
    &context,
    pathsToWatch,
    kFSEventStreamEventIdSinceNow,
    latency,
    kFSEventStreamCreateFlagNone
  );

  FSEventStreamScheduleWithRunLoop(stream, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  FSEventStreamStart(stream);
  CFRunLoopRun();
}

static VALUE t_init(VALUE self) {
  rb_iv_set(self, "@latency", rb_float_new(0.5));
  return self;
}

static VALUE t_on_change(VALUE self, VALUE original_directory_name) {
  rb_raise(rb_eNotImpError, "You must define #on_change in your subclass");
  return self;
}

static VALUE t_watch_directories(VALUE self, VALUE directories) {
  if (TYPE(directories) == T_ARRAY) {
    rb_iv_set(self, "@registered_directories", rb_ary_new4(RARRAY_LEN(directories), RARRAY_PTR(directories)));
  }
  else {
    rb_iv_set(self, "@registered_directories", rb_ary_new3(1, directories));
  }
  VALUE rb_registered_directories = rb_iv_get(self, "@registered_directories");
  return rb_registered_directories;
}

static VALUE t_start(VALUE self) {
  VALUE rb_registered_directories = rb_iv_get(self, "@registered_directories");
  Check_Type(rb_registered_directories, T_ARRAY);

  watch_directory(self);
  return self;
}

static VALUE t_stop(VALUE self) {
  FSEventStreamStop(stream);
  FSEventStreamInvalidate(stream);
  FSEventStreamRelease(stream);
  CFRunLoopStop(CFRunLoopGetCurrent());
  return self;
}

static VALUE t_restart(VALUE self) {
  t_stop(self);
  watch_directory(self);
  return self;
}

void delegate_signal_to_ruby(int signal) {
  VALUE signal_mod = rb_const_get(rb_cObject, rb_intern("Signal"));
  if (rb_funcall(signal_mod, rb_intern("handles?"), 1, INT2FIX(signal)) == Qtrue) {
    rb_funcall(signal_mod, rb_intern("handle"), 1, INT2FIX(signal));
  }
  else {
    ruby_default_signal(signal);
  }
}

void register_signal_delegation() {
  int i;
  for(i = 0; i < 33; i++) { // Signal.list.values.size yields 32 different signals
    (void) signal(i, delegate_signal_to_ruby);
  }
}

void Init_fsevent() {
  rb_require("fsevent/signal_ext");

  fsevent_class = rb_define_class("FSEvent", rb_cObject);
  rb_define_method(fsevent_class, "initialize", t_init, 0);
  rb_define_method(fsevent_class, "on_change", t_on_change, 1);
  rb_define_method(fsevent_class, "watch_directories", t_watch_directories, 1);
  rb_define_method(fsevent_class, "start", t_start, 0);
  rb_define_method(fsevent_class, "stop", t_stop, 0);
  rb_define_method(fsevent_class, "restart", t_restart, 0);

  rb_define_attr(fsevent_class, "latency", 1, 1);
  rb_define_attr(fsevent_class, "registered_directories", 1, 1);

  register_signal_delegation();
}
