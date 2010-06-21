
$LOAD_PATH.unshift File.expand_path('../ext', File.dirname(__FILE__))
require 'fsevent'

class PrintChange < FSEvent
  def on_change(dirs)
    puts "Detected change in: #{dirs.inspect}"
  end

  def start
    puts "watching #{directories.join(", ")} for changes"
    super
  end
end

printer = PrintChange.new
printer.latency = 0.2
printer.watch_directories %W(#{Dir.pwd} /tmp)
printer.start  # the start method no longer blocks

# You have to get the main ruby thread to sleep or wait on an IO stream
# otherwise the ruby interpreter will exit. My preferred method is to wait for
# the user to hit enter. The program will gracefuly exit when the main ruby
# thread gets to the end of the program.
STDIN.getc

