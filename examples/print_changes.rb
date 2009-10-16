require File.dirname(__FILE__) + '/../ext/fsevent'

class PrintChange < FSEvent
  def on_change(directories)
    puts "Detected change in: #{directories.inspect}"
  end

  def run
    puts "watching #{registered_directories.join(", ")} for changes"
    super
  end
end

printer = PrintChange.new
printer.latency = 0.2
printer.watch_directories %W(#{Dir.pwd} /tmp)
printer.run
