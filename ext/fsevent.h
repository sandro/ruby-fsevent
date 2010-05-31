
#include <ruby.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <CoreServices/CoreServices.h>


struct CFRunLoop_Struct {
  int pipes[2];
  pthread_t thread;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  CFRunLoopRef run_loop;
  bool running;
};
typedef struct CFRunLoop_Struct* CFRunLoop;


struct FSEvent_Struct {
  FSEventStreamRef stream;
};
typedef struct FSEvent_Struct* FSEvent;


bool is_fsevent_struct _(( VALUE ));
VALUE fsevent_rb_callback _(( VALUE, VALUE ));
void cfrunloop_signal _(( CFRunLoop ));
CFRunLoop cfrunloop_struct _(( VALUE ));

RUBY_EXTERN VALUE cfrunloop_class;
RUBY_EXTERN VALUE fsevent_class;

