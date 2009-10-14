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

  for (i = 0; i < numEvents; i++) {
    rb_funcall(self, rb_intern("directory_change"), 1, rb_str_new2(paths[i]));
  }
}

void watch_directory(VALUE self, char *directory_name) {

  CFStringRef mypath = CFStringCreateWithCString(NULL, directory_name, kCFStringEncodingUTF8);
  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);

  VALUE rb_latency = rb_iv_get(self, "@latency");
  CFAbsoluteTime latency = NUM2DBL(rb_latency);

  FSEventStreamContext context;
  context.version = 0;
  context.info = self;
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

static VALUE t_init(VALUE self, VALUE original_directory_name) {
  VALUE directory_name = StringValue(original_directory_name);
  rb_iv_set(self, "@directory_name", directory_name);
  rb_iv_set(self, "@latency", rb_float_new(0.5));
  return self;
}

static VALUE t_directory_change(VALUE self, VALUE original_directory_name) {
  return self;
}

static VALUE t_run(VALUE self) {
  VALUE directory_name = rb_iv_get(self, "@directory_name");
  watch_directory(self, RSTRING(directory_name)->ptr);
  return self;
}

VALUE watch_class;

void Init_watch() {
  watch_class = rb_define_class("Watch", rb_cObject);
  rb_define_method(watch_class, "initialize", t_init, 1);
  rb_define_method(watch_class, "directory_change", t_directory_change, 1);
  rb_define_method(watch_class, "run", t_run, 0);
}
