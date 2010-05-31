require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'tmpdir'

DirsArray = Class.new(FSEvent) do
  def dirs() @dirs ||= []; end
  def on_change( ary ) dirs.concat ary; end
end

describe FSEvent do
  describe "accessors" do
    it "reads and writes directories" do
      subject.directories = %w(one two)
      subject.directories.should == %w(one two)

      lambda { subject.watch Hash.new }.should raise_error(
        TypeError, 'directories must be given as a String or an Array of strings'
      )
    end

    it "reads and writes latency" do
      subject.latency = 1.5
      subject.latency.should == 1.5
    end
  end

  describe "iniitialization" do
    it "accepts a directory" do
      fs = FSEvent.new '/Users'
      fs.directories.should == %w(/Users)

      fs = FSEvent.new %w(/Users /tmp)
      fs.directories.should == %w(/Users /tmp)
    end

    it "accepts a latency" do
      fs = FSEvent.new 3.14159
      fs.latency.should == 3.14159
    end

    it "accepts both latency and directories" do
      fs = FSEvent.new %w(/tmp), 3.14159
      fs.directories.should == %w(/tmp)
      fs.latency.should == 3.14159
    end

    it "complains about unrecognized types" do
      lambda { FSEvent.new('/tmp', 'foo') }.should raise_error(
        TypeError, 'latency must be a Numeric value'
      )

      lambda { FSEvent.new({}, 1) }.should raise_error(
        TypeError, 'directories must be given as a String or an Array of strings'
      )
    end
  end

  describe "#watch" do
    it "register a single directory" do
      subject.watch '/Users'
      subject.directories.should == ['/Users']
    end

    it "registers an array of directories" do
      subject.watch %w(/Users /tmp)
      subject.directories.should == %w(/Users /tmp)
    end

    it "clears the array of directories" do
      subject.directories = %w(/Users /tmp)
      subject.directories.should == %w(/Users /tmp)
      subject.watch nil
      subject.directories.should be_nil
    end
  end

  describe "API" do
    it { should respond_to(:watch) }
    it { should respond_to(:stop) }
    it { should respond_to(:start) }
    it { should respond_to(:restart) }
    it { should respond_to(:running?) }
  end

  describe "when monitoring a directory for changes" do
    before(:all) {
      @dir = '/private' + Dir.tmpdir + "/#$$/"
      FileUtils.mkdir_p @dir
    }
    after(:all) { FileUtils.rm_rf @dir }

    before(:each) { @subject = DirsArray.new @dir, 0.1 }
    after(:each) { @subject.stop }

    it "picks up changes when running" do
      subject.should_not be_running
      subject.start
      subject.should be_running
      subject.dirs.should be_empty

      FileUtils.touch(@dir + 'test1.txt')
      Thread.pass while subject.dirs.empty?
      subject.dirs.should == [@dir]
    end

    it "does not pick up changes when stopped" do
      subject.should_not be_running

      FileUtils.touch(@dir + 'test2.txt')
      Thread.pass
      subject.dirs.should be_empty

      subject.start
      subject.should be_running
      Thread.pass
      subject.dirs.should be_empty

      FileUtils.touch(@dir + 'test3.txt')
      Thread.pass while subject.dirs.empty?
      subject.dirs.should == [@dir]
    end
  end

end

