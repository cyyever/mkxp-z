def check(value, message)
  raise message unless value
end

def expect_error(klass)
  begin
    yield
  rescue klass
    return
  end
  raise "expected #{klass}"
end

%w[@raw @short @unknownsize @noseek].each do |kind|
  closed = fileint_test_closed
  bytes = load_data(kind, true)
  check(bytes.bytes == [97, 0, 255, 122], "#{kind}: whole-file binary data changed")
  check(bytes.encoding == Encoding::BINARY, 'raw load must remain a binary String')
  check(fileint_test_live == 0 && fileint_test_closed == closed + 1, "#{kind}: stream not closed exactly once")
end
check(load_data('@empty', true) == ''.b, 'empty raw file should be an empty binary String')

[['@error', IOError], ['@partialerror', IOError], ['@closeerror', IOError],
 ['@oversize', RangeError], ['@badmarshal', TypeError]].each do |kind, exception|
  closed = fileint_test_closed
  expect_error(exception) { load_data(kind, kind != '@badmarshal') }
  GC.start
  check(fileint_test_live == 0 && fileint_test_closed == closed + 1, "#{kind}: failure or GC closed stream incorrectly")
end

# Ruby compaction may run while native IO has released the GVL. Native reads
# must not retain a pointer into an embedded/movable Ruby String.
running = true
collector = Thread.new do
  while running
    GC.start
    GC.compact
    Thread.pass
  end
end
begin
  20.times { check(load_data('@slow', true).bytes == [97, 0, 255, 122], 'GC/compaction corrupted native read') }
ensure
  running = false
  collector.join
end
check(fileint_test_live == 0, 'GC stress retained a native stream')

closed = fileint_test_closed
reader = Thread.new { load_data('@slow', true) }
Thread.pass while reader.alive? && fileint_test_live == 0
reader.kill
reader.join
check(fileint_test_live == 0 && fileint_test_closed == closed + 1, 'interrupted IO did not ensure exactly-once cleanup')

class BinaryPayload
  def _dump(_depth) = "\0\xff".b
  def self._load(bytes)
    raise 'Marshal opaque payload changed' unless bytes.bytes == [0, 255]
    new
  end
end
filename = "file-io-#{Process.pid}.rxdata"
begin
  payload = ["雨血", "a\0z", [true, nil, 42], BinaryPayload.new]
  save_data(payload, filename)
  loaded = load_data(filename)
  check(loaded[0, 3] == payload[0, 3], 'Marshal text/data roundtrip changed')
  check(loaded[3].is_a?(BinaryPayload), 'Marshal custom object lost')
  check(load_data(filename, true).bytes == File.binread(filename).bytes, 'raw Marshal bytes changed')
  module Marshal
    class << self
      alias io_test_original_dump dump
      attr_reader :io_test_port
      def dump(_object, port)
        @io_test_port = port
        raise 'injected Marshal dump failure'
      end
    end
  end
  expect_error(RuntimeError) { save_data(nil, filename) }
  check(Marshal.io_test_port.closed?, 'save_data failure left Ruby File open')
ensure
  File.delete(filename) if File.exist?(filename)
end
check(fileint_test_load, 'native SDL load changed binary bytes, failure output, or borrowed ownership')
check(fileint_test_live == 0, 'native stream leaked')
puts 'Ruby/SDL file IO checks passed'
