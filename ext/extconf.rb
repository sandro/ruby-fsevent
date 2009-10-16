require 'mkmf'
with_ldflags("-framework CoreServices") do
  create_makefile("fs_event")
end
