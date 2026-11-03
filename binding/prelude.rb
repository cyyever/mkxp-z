# encoding: utf-8

# Engine APIs that plain Ruby already expresses. The engine evaluates this at
# startup, right after its native classes and modules are defined, so these
# stay part of the public scripting API without native code.

# Selection highlights keep their original 40 Hz timing when game logic is
# accelerated. Action animations, damage flashes and visibility stay untouched.
module MKXPBlinkTiming
  def blink_on
    @_mkxp_blink_started_at = Process.clock_gettime(Process::CLOCK_MONOTONIC) unless @_blink
    super
  end

  def update
    if @_blink
      now = Process.clock_gettime(Process::CLOCK_MONOTONIC)
      @_mkxp_blink_started_at ||= now
      @_blink_count = ((now - @_mkxp_blink_started_at) * 40).to_i % 32
    end
    super
  end
end

RPG::Sprite.prepend(MKXPBlinkTiming)

module System
  @started_at = Process.clock_gettime(Process::CLOCK_MONOTONIC)

  class << self
    # Seconds since the scripting runtime started.
    def uptime = Process.clock_gettime(Process::CLOCK_MONOTONIC) - @started_at
    alias delta uptime

    # Write a line to the engine's diagnostic output (stderr).
    def puts(text)
      $stderr.puts(String(text))
      nil
    end

    def is_windows? = RUBY_PLATFORM.match?(/mswin|mingw|cygwin/)
    def is_mac? = RUBY_PLATFORM.include?('darwin')
    def is_linux? = RUBY_PLATFORM.include?('linux')
    def is_bsd? = RUBY_PLATFORM.include?('bsd')

    # The login name from the environment, or nil if unset.
    def user_name = ENV.fetch(is_windows? ? 'USERNAME' : 'USER', nil)
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
  def to_s = format('(%f, %f, %f, %f)', red, green, blue, alpha)
  alias inspect to_s
  alias === ==
  alias eql? ==
end

class Tone
  def to_s = format('(%f, %f, %f, %f)', red, green, blue, gray)
  alias inspect to_s
  alias === ==
  alias eql? ==
end

class Rect
  def to_s = format('(%d, %d, %d, %d)', x, y, width, height)
  alias inspect to_s
  alias === ==
  alias eql? ==

  def empty = set(0, 0, 0, 0)
end

class Bitmap
  def rect = Rect.new(0, 0, width, height)

  # Filling with a fully transparent color is the clear operation.
  def clear_rect(*args)
    unless [1, 4].include?(args.size)
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
  def to_utf8 = dup.to_utf8!

  def to_utf8!
    force_encoding(Encoding::UTF_8)
    raise MKXPError, 'Invalid UTF-8 text' unless valid_encoding?

    # Byte-wise, so it does not depend on this file's source encoding.
    replace(byteslice(3..)) if byteslice(0, 3).bytes == [0xEF, 0xBB, 0xBF]
    self
  end
end

# Map font family names a game asks for to fonts installed on this system.
# The game's own Fonts/ and fontSub still win: a name is only looked up here
# when the engine does not already know it. Chinese family names that RPG
# Maker games use (黑体/楷体/宋体 ...) also map to each platform's equivalent.
module MKXPSystemFonts
  STYLES = {
    sans: ['SimHei', 'Microsoft YaHei', 'Heiti SC', 'STHeiti', 'PingFang SC',
           'Hiragino Sans GB', 'Noto Sans CJK SC', 'Noto Sans SC', 'Source Han Sans SC',
           'Source Han Sans CN', 'WenQuanYi Micro Hei', 'WenQuanYi Zen Hei',
           'Droid Sans Fallback'],
    kai: ['KaiTi', 'KaiTi_GB2312', 'Kaiti SC', 'STKaiti', 'LXGW WenKai',
          'AR PL UKai CN', 'AR PL KaitiM GB'],
    serif: ['SimSun', 'NSimSun', 'Songti SC', 'STSong', 'Noto Serif CJK SC',
            'Noto Serif SC', 'Source Han Serif SC', 'Source Han Serif CN',
            'AR PL UMing CN', 'AR PL SungtiL GB']
  }.freeze

  REQUESTS = {
    '黑体' => :sans, '黑體' => :sans, 'simhei' => :sans, '微软雅黑' => :sans,
    'microsoft yahei' => :sans,
    '楷体' => :kai, '楷體' => :kai, 'kaiti' => :kai, '楷体_gb2312' => :kai,
    'kaiti_gb2312' => :kai,
    '宋体' => :serif, '宋體' => :serif, 'simsun' => :serif, '新宋体' => :serif,
    'nsimsun' => :serif
  }.freeze

  FAMILY_NAME_IDS = [1, 16].freeze # family, typographic family

  class << self
    # The names a script asked for, with each missing one replaced by an
    # installed equivalent. Keeps the shape (String or Array) it was given.
    def resolve(names)
      case names
      in String then locate(names) || names
      in Array then names.map { it.is_a?(String) ? locate(it) || it : it }
      else names
      end
    end

    # A family name the engine can render for +name+, registering a system
    # font file when needed; nil if neither the game nor the system has one.
    def locate(name)
      return name if Font._mkxp_native_exist?(name)

      candidates = REQUESTS.key?(name.downcase) ? STYLES[REQUESTS[name.downcase]] : [name]
      candidates.find do |candidate|
        next true if Font._mkxp_native_exist?(candidate)

        path = index[candidate.downcase]
        path && Font.register_file(path) && Font._mkxp_native_exist?(candidate)
      end
    end

    private

    # Lowercase family name -> font file, built once on first use.
    def index
      @index ||= font_files.each_with_object({}) do |path, map|
        family_names(path).each { map[it.downcase] ||= path }
      end
    end

    def font_files = font_dirs.flat_map { Dir.glob("#{it}/**/*.{ttf,otf,ttc}", File::FNM_CASEFOLD) }.uniq

    def env(key) = ENV.fetch(key, nil)&.then { it.empty? ? nil : it.tr('\\', '/') }

    def font_dirs
      home = env('HOME')
      dirs =
        if System.is_windows?
          [env('WINDIR')&.+('/Fonts'), env('LOCALAPPDATA')&.+('/Microsoft/Windows/Fonts')]
        elsif System.is_mac?
          ['/System/Library/Fonts', '/Library/Fonts', home&.+('/Library/Fonts'),
           *Dir.glob('/System/Library/AssetsV2/com_apple_MobileAsset_Font*')]
        else
          data_home = env('XDG_DATA_HOME') || home&.+('/.local/share')
          data_dirs = (env('XDG_DATA_DIRS') || '/usr/local/share:/usr/share').split(':')
          [data_home, *data_dirs].compact.map { "#{it}/fonts" } << home&.+('/.fonts')
        end
      dirs.compact.uniq.select { File.directory?(it) }
    end

    # Family names of the first face, read from the SFNT table directory and
    # 'name' table only, never the whole file.
    def family_names(path)
      File.open(path, 'rb') do |file|
        header = file.read(12)
        # A collection's first face offset follows its 12-byte header.
        if header&.start_with?('ttcf')
          file.seek(file.read(4).unpack1('N'))
          header = file.read(12)
        end
        directory = file.read(header.unpack1('@4n') * 16)
        _tag, _checksum, offset, length = directory.scan(/.{16}/m).map { it.unpack('a4NNN') }
                                                   .find { it.first == 'name' }
        return [] unless offset

        file.seek(offset)
        table = file.read(length)
        count, strings = table.unpack('@2nn')
        Array.new(count) { table.byteslice(6 + (it * 12), 12).unpack('n6') }.filter_map do |record|
          platform, encoding, _language, id, size, start = record
          decode(table.byteslice(strings + start, size), platform, encoding) if FAMILY_NAME_IDS.include?(id)
        end.uniq
      end
    rescue SystemCallError, IOError, NoMethodError, TypeError, ArgumentError, RangeError
      []
    end

    # UTF-16BE/UCS-4 names by hand: the embedded Ruby has no transcoders.
    def decode(raw, platform, encoding)
      return nil if raw.nil? || raw.empty?

      units =
        case [platform, encoding]
        in [3, 10] then raw.unpack('N*')
        in [0, _] | [3, 1] then raw.unpack('n*')
        in [1, _] then return raw.dup.force_encoding(Encoding::UTF_8).then { it.ascii_only? ? it : nil }
        else return nil
        end
      codepoints = []
      until units.empty?
        unit = units.shift
        if unit.between?(0xD800, 0xDBFF) && units.first&.between?(0xDC00, 0xDFFF)
          unit = 0x10000 + ((unit - 0xD800) << 10) + (units.shift - 0xDC00)
        end
        return nil if unit.between?(0xD800, 0xDFFF) || unit > 0x10FFFF

        codepoints << unit
      end
      codepoints.pack('U*')
    end
  end
end

class Font
  class << self
    alias _mkxp_native_exist? exist?
    alias _mkxp_native_default_name= default_name=

    def exist?(name) = !MKXPSystemFonts.locate(name.to_s).nil?

    def default_name=(names)
      self._mkxp_native_default_name = MKXPSystemFonts.resolve(names)
    end
  end

  alias _mkxp_native_initialize initialize
  alias _mkxp_native_name= name=

  def initialize(*args)
    args[0] = MKXPSystemFonts.resolve(args[0]) unless args.empty?
    _mkxp_native_initialize(*args)
  end

  def name=(names)
    self._mkxp_native_name = MKXPSystemFonts.resolve(names)
  end
end
