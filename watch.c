#include "ruby.h"
/* #include "fswatch.c" */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <CoreServices/CoreServices.h>


VALUE watch_class;

void callback(
    ConstFSEventStreamRef streamRef,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
  int i;
  char **paths = eventPaths;

  for (i = 0; i < numEvents; i++) {
    printf("%s\n", paths[i]);
  }
  exit(0);
}

void watch_directory(VALUE self, char *directory_name) {
  printf("Watching dir\n");
  printf("%s\n\n", directory_name);
  rb_funcall(self, rb_intern("something"), 0);

  CFStringRef mypath = CFStringCreateWithCString(NULL, directory_name, kCFStringEncodingUTF8);
  CFArrayRef pathsToWatch = CFArrayCreate(NULL, (const void **)&mypath, 1, NULL);
  void *callbackInfo = NULL;
  FSEventStreamRef stream;
  CFAbsoluteTime latency = 0.5;

  stream = FSEventStreamCreate(NULL,
    &callback,
    callbackInfo,
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

void Init_watch() {
  watch_class = rb_define_class("Watch", rb_cObject);
  rb_define_method(watch_class, "initialize", t_init, 1);
  rb_define_method(watch_class, "directory_change", t_directory_change, 1);
  rb_define_method(watch_class, "run", t_run, 0);
}
