require 'mkmf'
with_ldflags("-framework CoreServices") do
  create_makefile("fsevent")
end
