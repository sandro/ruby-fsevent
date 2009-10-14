require 'watch'

class LibWatch < Watch
  def initialize(dir)
    super(dir)
    @latency = 0.5
  end

  def directory_change(directory)
    puts "Ruby callback: #{directory}"
  end
end

l = LibWatch.new('.')
l.run
