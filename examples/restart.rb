$LOAD_PATH.unshift File.expand_path('../ext', File.dirname(__FILE__))
require 'fsevent'

class Restart < FSEvent
  def on_change(dirs)
    puts "Detected change in: #{dirs.inspect}"
    unless @restarted
      @restarted = true
      self.watch_directories "#{Dir.pwd}/spec"
      self.restart
    end
  end

  def start
    puts "watching #{directories.join(", ")} for changes"
    super
  end
end

restarter = Restart.new
restarter.watch_directories "#{Dir.pwd}/examples"
restarter.start  # the start method no longer blocks

# You have to get the main ruby thread to sleep or wait on an IO stream
# otherwise the ruby interpreter will exit. My preferred method is to wait for
# the user to hit enter. The program will gracefuly exit when the main ruby
# thread gets to the end of the program.
STDIN.getc

