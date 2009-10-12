require 'watch'

trap("INT") do
  puts 'Signal trapped'
  exit 0
end

class LibWatch < Watch
  def initialize(dir)
    super(dir)
    @latency = 0.5
  end

  def something
    puts "something called"
  end

  def directory_change(directory)
    puts "Ruby callback: #{directory}"
    exit 0
  end
end

l = LibWatch.new('.')
l.run
