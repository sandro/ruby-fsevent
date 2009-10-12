#include "ruby.h"
/* #include "fswatch.c" */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <CoreServices/CoreServices.h>

void callback(
    ConstFSEventStreamRef streamRef,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
  int i;

  for (i = 0; i < numEvents; i++) {
    printf("%s\n", ((char **)eventPaths)[i]);
  }
}

//set up fsevents and callback
void watch_directory(char *directory_name) {
  printf("Watching dir\n");
  printf("%s\n\n", directory_name);

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
  watch_directory(RSTRING(directory_name)->ptr);
  return self;
}

static VALUE t_directory_change(VALUE self, VALUE original_directory_name) {
  return self;
}

VALUE watch_class;

void Init_watch() {
  watch_class = rb_define_class("Watch", rb_cObject);
  rb_define_method(watch_class, "initialize", t_init, 1);
  rb_define_method(watch_class, "directory_change", t_directory_change, 1);
}
