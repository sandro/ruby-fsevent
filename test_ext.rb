require 'watch'

class LibWatch < Watch
  attr_accessor :latency
  attr_reader :watch_pid

  def directory_change(directory)
    puts "Ruby callback: #{directory.inspect}"
  end
end

l = LibWatch.new
l.latency = 0.2
# l.watch_directories "."
l.watch_directories %w(. /tmp)
l.run
