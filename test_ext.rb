require 'watch'

class LibWatch < Watch
  def on_change(directory)
    puts "Ruby callback: #{directory.inspect}"
  end
end

l = LibWatch.new

p l.latency
l.latency = 0.2
p l.latency

# l.watch_directories "."
l.watch_directories %w(. /tmp)

l.run
