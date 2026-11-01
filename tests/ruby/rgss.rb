def check(value, message)
  raise message unless value
end

# MRI installs generated rbconfig.rb in the platform-specific subdirectory.
# The test's RUBYLIB points at the shared versioned library directory.
if (runtime_lib = ENV['RUBYLIB'])
  arch_lib = Dir.glob(File.join(runtime_lib, '*')).find do |path|
    File.file?(File.join(path, 'rbconfig.rb'))
  end
  $LOAD_PATH.unshift(arch_lib) if arch_lib
end

check(RUBY_VERSION == '3.4.12', "unexpected runtime: #{RUBY_DESCRIPTION}")
check(verify_native_class, 'native class replacement')
check(verify_typed_data == 42, 'C++ typed data')

class Visibility
  private def hidden = 41
  protected def guarded = 42
end
object = Visibility.new
# Repeat to exercise both uncached and cached VM dispatch.
100.times do
  check(object.hidden == 41, 'private dispatch')
  check(object.guarded == 42, 'protected dispatch')
  check(object.public_send(:hidden) == 41, 'public_send dispatch')
end

class Reopened < Array; end
class Reopened < Hash; end
check(Reopened.superclass == Array, 'RGSS class reopening keeps the original superclass')

debug, verbose = $DEBUG, $VERBOSE
begin
  $DEBUG = true
  $VERBOSE = true
  check(sprintf('%s', 'RGSS', :ignored) == 'RGSS', 'surplus sprintf arguments')
ensure
  $DEBUG, $VERBOSE = debug, verbose
end

# zlib is required by the packaged game runtime.
require 'zlib'

# The autoconf installer loads RubyGems and needs the monitor extension. MSVC
# links directly from its build tree and does not run that installer.
unless RUBY_PLATFORM.include?('mswin')
  require 'monitor'
  check(Monitor.new.synchronize { 42 } == 42, 'Unix installer monitor extension')
end

payload = [1, -1, 2**80, "\x00\xff".b, ["日本語", {rgss: 3}]]
check(Marshal.load(Zlib::Inflate.inflate(Zlib::Deflate.deflate(Marshal.dump(payload)))) == payload,
      'compressed RGSS script/data round trip')
check(Marshal.load("\x04\x08[\x08i\x06i\x00i\xFA".b) == [1, 0, -1], 'legacy Marshal 4.8 fixture')
check("日本語".encode('Windows-31J').encode('UTF-8') == "日本語", 'RGSS encoding conversion')
check(Thread.new { 42 }.value == 42, 'thread startup')
check(Fiber.new { Fiber.yield 1; 2 }.resume == 1, 'fiber startup')
GC.start
puts "Ruby #{RUBY_VERSION}: embedded RGSS compatibility checks passed"
