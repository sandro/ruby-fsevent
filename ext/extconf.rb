require 'mkmf'

$defs << "-DRUBY_VERSION_CODE=#{RUBY_VERSION.gsub(/\D/, '')}"

with_ldflags("-framework CoreServices") do
  create_makefile("fsevent")
end
