require 'watch'

trap("INT") do
  puts 'trapped'
  exit 0
end

class LibWatch < Watch
  def directory_change(file)
    puts file
  end
end

l = LibWatch.new('.')
