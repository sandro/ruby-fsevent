require File.expand_path(File.dirname(__FILE__) + '/spec_helper')

describe FSEvent do
  describe "accessors" do
    it "reads and writes registered_directories" do
      subject.registered_directories = %w(one two)
      subject.registered_directories.should == %w(one two)
    end

    it "reads and writes latency" do
      subject.latency = 1.5
      subject.latency.should == 1.5
    end
  end

  describe "#watch_directories" do
    it "register a single directory" do
      subject.watch_directories '/Users'
      subject.registered_directories.should == ['/Users']
    end
    it "registers an array of directories" do
      subject.watch_directories %w(/Users /tmp)
      subject.registered_directories.should == %w(/Users /tmp)
    end
  end

  describe "#on_change" do
    it "raises NotImplementedError" do
      expect do
        subject.on_change(nil)
      end.to raise_error(NotImplementedError)
    end
  end

  describe "API" do
    it { should respond_to(:on_change) }
    it { should respond_to(:start) }
    it { should respond_to(:stop) }
    it { should respond_to(:restart) }
  end
end
