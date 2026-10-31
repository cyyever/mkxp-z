def check(value, message)
  raise message unless value
end

check(RUBY_VERSION == '4.0.9', "unexpected runtime: #{RUBY_DESCRIPTION}")
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

# These must load from the packaged runtime with RubyGems disabled.
%w[zlib json psych bigdecimal fiddle nkf racc/cparse digest/md5 digest/sha2
   date stringio strscan socket pathname erb/escape csv base64 logger].each do |name|
  require name
end
require 'win32ole' if RUBY_PLATFORM.include?('mingw')
require 'syslog' unless RUBY_PLATFORM.include?('mingw')
require 'bigdecimal/util'
check(BigDecimal('1.25') * 2 == BigDecimal('2.5'), 'BigDecimal')
check(Fiddle::SIZEOF_VOIDP > 0, 'Fiddle')
check(NKF.nkf('-w', "RGSS") == 'RGSS', 'NKF')
check(JSON.parse('{"rgss":3}')['rgss'] == 3, 'JSON')
check(Psych.safe_load('rgss: 3')['rgss'] == 3, 'YAML')

payload = [1, -1, 2**80, "\x00\xff".b, ["日本語", {rgss: 3}]]
check(Marshal.load(Zlib::Inflate.inflate(Zlib::Deflate.deflate(Marshal.dump(payload)))) == payload,
      'compressed RGSS script/data round trip')
check(Marshal.load("\x04\x08[\x08i\x06i\x00i\xFA".b) == [1, 0, -1], 'legacy Marshal 4.8 fixture')
check("日本語".encode('Windows-31J').encode('UTF-8') == "日本語", 'RGSS encoding conversion')
check(Thread.new { 42 }.value == 42, 'thread startup')
check(Fiber.new { Fiber.yield 1; 2 }.resume == 1, 'fiber startup')
GC.start
puts "Ruby #{RUBY_VERSION}: embedded RGSS compatibility checks passed"
