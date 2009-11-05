ruby-fsevent
============

A native extension exposing the OS X FSEvent API.

Register the directories you want to watch, create a custom callback, and your
callback will fire everytime a change occurs in the registered directories.

I'm tired of recompiling RubyCocoa just get Rspactor working whenever I switch Ruby versions. Hopefully, this gem will break that dependency.

Watchr and Kicker have recently grabbed my attention. They allow you to watch
directories for changes and create custom event handlers. Unfortunately, Watchr
has a 4-5 second delay when using it with Rev (recommended) before your
callback is fires.  Without Rev, Watchr degrades to a 0.5 second Ruby loop
which we all know is not resource-friendly. Now, Watchr could easily add native
FSEvents as a new backend for Mac users.

Kicker already uses the FSEvents API but it requires RubyCocoa just like
Rspactor. They could easily switch their dependency to this native extension
and remove the need for RubyCocoa.

With a generic, native interface to the FSEvent API we, as Rubyists can harness
the power of OSX FSEvents without depending on RubyCocoa.

Demo
----

1. rake make
2. ruby examples/print_changes.rb
3. Notice that the examples directory and the /tmp directory are being monitored
4. Make a change to either directory and watch the callback fire

TODO
----

* Add ability to register a block as a callback handler, on_change would then
call the block. This removes the need for subclassing.

Note on Patches/Pull Requests
-----------------------------

* Fork the project.
* Make your feature addition or bug fix.
* Add tests for it. This is important so I don't break it in a
  future version unintentionally.
* Commit, do not mess with rakefile, version, or history.
  (if you want to have your own version, that is fine but
   bump version in a commit by itself I can ignore when I pull)
* Send me a pull request. Bonus points for topic branches.

Copyright
---------

Copyright (c) 2009 Sandro Turriate. See LICENSE for details.
