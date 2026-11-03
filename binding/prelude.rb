# Engine APIs that plain Ruby already expresses. The engine evaluates this at
# startup, right after its native classes and modules are defined, so these
# stay part of the public scripting API without native code.

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

module Graphics
  # RGSS3: the same as calling Graphics.update duration times.
  def self.wait(duration)
    Integer(duration).times { update }
    nil
  end
end

class Color
  def to_s
    format('(%f, %f, %f, %f)', red, green, blue, alpha)
  end
  alias inspect to_s
  alias === ==
  alias eql? ==
end

class Tone
  def to_s
    format('(%f, %f, %f, %f)', red, green, blue, gray)
  end
  alias inspect to_s
  alias === ==
  alias eql? ==
end

class Rect
  def to_s
    format('(%d, %d, %d, %d)', x, y, width, height)
  end
  alias inspect to_s
  alias === ==
  alias eql? ==

  def empty
    set(0, 0, 0, 0)
  end
end

class Bitmap
  def rect
    Rect.new(0, 0, width, height)
  end

  # Filling with a fully transparent color is the clear operation.
  def clear_rect(*args)
    unless args.size == 1 || args.size == 4
      raise ArgumentError, "wrong number of arguments (given #{args.size}, expected 1 or 4)"
    end
    fill_rect(*args, Color.new(0, 0, 0, 0))
  end
end

class Sprite
  def width
    raise RGSSError, 'disposed sprite' if disposed?
    src_rect.width
  end

  def height
    raise RGSSError, 'disposed sprite' if disposed?
    src_rect.height
  end
end

class String
  # Validate as UTF-8 and drop a leading byte order mark.
  def to_utf8
    dup.to_utf8!
  end

  def to_utf8!
    force_encoding(Encoding::UTF_8)
    raise MKXPError, 'Invalid UTF-8 text' unless valid_encoding?
    # Byte-wise, so it does not depend on this file's source encoding.
    if getbyte(0) == 0xEF && getbyte(1) == 0xBB && getbyte(2) == 0xBF
      replace(byteslice(3, bytesize - 3))
    end
    self
  end
end
