module Signal
  class << self
    alias trap_without_custom_handlers trap

    def handlers
      @handlers ||= {}
    end

    def trap(signal, &block)
      handlers[int_for_signal(signal)] = block
    end

    def handles?(signal)
      handlers.has_key? int_for_signal(signal)
    end

    def handle(signal)
      if handler = handlers[int_for_signal(signal)]
        handler.call
      end
    end

    protected

    def int_for_signal(signal)
      error_msg = "Check Signal.list for a list of valid signals."
      case signal
      when Numeric
        list.values.include?(signal) ? signal : raise(ArgumentError, "Invalid signal number. #{error_msg}")
      when String
        signal = signal.upcase
        list.keys.include?(signal) ? list[signal] : raise(ArgumentError, "Invalid signal identifier. #{error_msg}")
      else
        raise ArgumentError, error_msg
      end
    end
  end
end

module Kernel
  def self.trap(signal, &block)
    Signal.trap(signal, &block)
  end
end

def trap(signal, &block)
  Signal.trap(signal, &block)
end
