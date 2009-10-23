require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe Signal do
  describe "#trap" do
    it "captures the block argument as the signal handler" do
      Signal.trap(2) { :int }
      Signal.handlers[2].call.should == :int
    end

    it "converts the signal name into an integer when storing the handler" do
      Signal.trap('INT')
      Signal.handlers.should include(Signal.list['INT'])
    end

    it "allows lowercase signal names" do
      Signal.trap('int')
      Signal.handlers.should include(Signal.list['INT'])
    end

    it "does not allow unknown signal names" do
      expect {
        Signal.trap('interrupt')
      }.to raise_error(ArgumentError)
    end

    it "does not allow unknown signal numbers" do
      expect {
        Signal.trap(12111211221)
      }.to raise_error(ArgumentError)
    end

    it "does not allow unknown data types" do
      expect {
        Signal.trap(['int'])
      }.to raise_error(ArgumentError)
    end
  end

  describe "#handles?" do
    it "returns true when a handler is registered for the signal" do
      Signal.trap('INT')
      Signal.handles?('INT').should be_true
    end

    it "returns false when no handler was registered for the signal" do
      Signal.handles?('EXIT').should be_false
    end
  end

  describe "#handle" do
    it "does not raise when trying to call a non-existant handler" do
      expect do
        Signal.handle('EXIT')
      end.should_not raise_error
    end

    it "calls the handler" do
      Signal.trap('INT') { :int }
      Signal.handle('INT').should == :int
    end
  end
end

describe Kernel do
  it "delegates .trap to Signal" do
    Signal.should_receive(:trap).with('INT')
    Kernel.trap('INT')
  end
end

context "global" do
  it "delegates .trap to Signal" do
    Signal.should_receive(:trap).with('INT')
    trap('INT')
  end
end
