require 'watch'

trap("INT") do
  puts 'trapped'
  exit 0
end

class LibWatch < Watch
  def something
    puts "something called"
  end
  def directory_change(file)
    puts "ruby callback"
    puts file
  end
end

l = LibWatch.new('.')

at_exit do
  puts "exiting"
  p l
end

l.run
