$:.unshift(File.dirname(__FILE__) + '/../ext')
$:.unshift(File.dirname(__FILE__) + '/../lib')
require 'fsevent'
require 'fsevent/signal_ext'
require 'spec/autorun'
