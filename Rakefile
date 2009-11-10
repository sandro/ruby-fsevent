require 'rubygems'
require 'rake'

begin
  require 'jeweler'
  Jeweler::Tasks.new do |gem|
    gem.name = "ruby-fsevent"
    gem.summary = %Q{A native extension exposing the OS X FSEvent API.}
    gem.description = %Q{
    A native extension exposing the OS X FSEvent API. Register directories you want to watch and a callback will fire whenever a change occurs in the registered directories.
    }
    gem.email = "sandro.turriate@gmail.com"
    gem.homepage = "http://github.com/sandro/ruby-fsevent"
    gem.authors = ["Sandro Turriate"]
    gem.add_development_dependency "rspec", '1.2.9'
    gem.require_paths = %w(lib ext)
    gem.extensions << 'ext/extconf.rb'
    # gem is a Gem::Specification... see http://www.rubygems.org/read/chapter/20 for additional settings
  end
  Jeweler::GemcutterTasks.new
rescue LoadError
  puts "Jeweler (or a dependency) not available. Install it with: sudo gem install jeweler"
end

require 'spec/rake/spectask'
Spec::Rake::SpecTask.new(:spec) do |spec|
  spec.libs << 'lib' << 'spec'
  spec.spec_files = FileList['spec/**/*_spec.rb']
end

Spec::Rake::SpecTask.new(:rcov) do |spec|
  spec.libs << 'lib' << 'spec'
  spec.pattern = 'spec/**/*_spec.rb'
  spec.rcov = true
end

task :spec => :check_dependencies

task :default => :spec

task :make do
  system "cd ext && ruby extconf.rb && make"
end

require 'rake/rdoctask'
Rake::RDocTask.new do |rdoc|
  if File.exist?('VERSION')
    version = File.read('VERSION')
  else
    version = ""
  end

  rdoc.rdoc_dir = 'rdoc'
  rdoc.title = "ruby-fsevent #{version}"
  rdoc.rdoc_files.include('README*')
  rdoc.rdoc_files.include('lib/**/*.rb')
end
