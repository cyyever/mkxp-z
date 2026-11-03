# Parts of the System module that plain Ruby already provides. The engine
# evaluates this at startup, right after it defines the rest of System, so
# these stay part of the public scripting API without native code.
module System
  @started_at = Process.clock_gettime(Process::CLOCK_MONOTONIC)

  class << self
    # Seconds since the scripting runtime started.
    def uptime
      Process.clock_gettime(Process::CLOCK_MONOTONIC) - @started_at
    end
    alias delta uptime

    # Write a line to the engine's diagnostic output (stderr).
    def puts(text)
      $stderr.puts(String(text))
      nil
    end

    def is_windows?
      RUBY_PLATFORM.match?(/mswin|mingw|cygwin/)
    end

    def is_mac?
      RUBY_PLATFORM.include?('darwin')
    end

    def is_linux?
      RUBY_PLATFORM.include?('linux')
    end

    def is_bsd?
      RUBY_PLATFORM.include?('bsd')
    end

    # The login name from the environment, or nil if unset.
    def user_name
      ENV[is_windows? ? 'USERNAME' : 'USER']
    end
  end
end
