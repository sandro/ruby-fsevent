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
  dir_size = RARRAY(rb_registered_directories)->len;

  VALUE *rb_dir_names = RARRAY(rb_registered_directories)->ptr;
  CFStringRef dir_names[dir_size];
  for (i = 0; i < dir_size; i++) {
    dir_names[i] = CFStringCreateWithCString(NULL, (char *)RSTRING(rb_dir_names[i])->ptr, kCFStringEncodingUTF8);
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

  FSEventStreamRef stream;
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
  return Qnil;
}

static VALUE t_watch_directories(VALUE self, VALUE directories) {
  if (TYPE(directories) == T_ARRAY) {
    rb_iv_set(self, "@registered_directories", rb_ary_new4(RARRAY(directories)->len, RARRAY(directories)->ptr));
  }
  else {
    rb_iv_set(self, "@registered_directories", rb_ary_new3(1, directories));
  }
  VALUE rb_registered_directories = rb_iv_get(self, "@registered_directories");
  return rb_registered_directories;
}

int pid, status;
static VALUE t_run(VALUE self) {
  VALUE rb_registered_directories = rb_iv_get(self, "@registered_directories");
  Check_Type(rb_registered_directories, T_ARRAY);
  if (pid = fork()) {
    wait(&status);
  }
  else {
    watch_directory(self);
  }
  return self;
}

void kill_watcher() {
  if (pid) {
    kill(pid, SIGKILL);
    printf("\n");
  }
}

VALUE fs_event_class;
void Init_fs_event() {
  fs_event_class = rb_define_class("FsEvent", rb_cObject);
  rb_define_method(fs_event_class, "initialize", t_init, 0);
  rb_define_method(fs_event_class, "on_change", t_on_change, 1);
  rb_define_method(fs_event_class, "watch_directories", t_watch_directories, 1);
  rb_define_method(fs_event_class, "run", t_run, 0);

  rb_define_attr(fs_event_class, "latency", 1, 1);
  rb_define_attr(fs_event_class, "registered_directories", 1, 1);

  atexit(kill_watcher);
}