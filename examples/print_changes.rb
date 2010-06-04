$LOAD_PATH.unshift(File.dirname(__FILE__) + '/../lib')
$LOAD_PATH.unshift(File.dirname(__FILE__) + '/../ext')
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
printer.start
