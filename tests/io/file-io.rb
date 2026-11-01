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

stream = fileint_test_open('@raw')
check(stream.read(0) == ''.b, 'zero read should be empty binary string')
check(stream.read(20).bytes == [97, 0, 255, 122], 'short read exposed extra/uninitialized bytes')
check(stream.read(2).nil?, 'length-limited EOF should be nil')
check(stream.read == ''.b, 'read-all EOF should be empty string')
stream.close
stream.close
check(fileint_test_live == 0, 'close retained stream')
closed_count = fileint_test_closed
stream = nil
GC.start
check(fileint_test_closed == closed_count, 'GC closed an explicitly closed stream twice')
stream = fileint_test_open('@raw')
stream.close
expect_error(IOError) { stream.read(1) }
expect_error(IOError) { stream.getbyte }

stream = fileint_test_open('@raw')
expect_error(ArgumentError) { stream.read(-1) }
check(stream.getbyte == 97, 'getbyte failed')
check(stream.read.bytes == [0, 255, 122], 'read-all changed position or binary bytes')
stream.close

stream = fileint_test_open('@error')
expect_error(IOError) { stream.read(1) }
expect_error(IOError) { stream.getbyte }
stream.close
%w[@unknownsize @noseek].each do |kind|
  stream = fileint_test_open(kind)
  expect_error(IOError) { stream.read }
  stream.close
end
stream = fileint_test_open('@closeerror')
expect_error(IOError) { stream.close }
stream.close
check(fileint_test_live == 0, 'failed close kept a freed stream pointer')

check(load_data('@raw', true).bytes == [97, 0, 255, 122], 'raw load changed binary data')
expect_error(TypeError) { load_data('@badmarshal') }
expect_error(IOError) { load_data('@error', true) }
check(fileint_test_live == 0, 'load_data failure did not ensure close')

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
check(fileint_test_buffer, 'short-buffer putback/error or borrowed ownership failed')
check(fileint_test_live == 0, 'native stream leaked')
puts 'Ruby/SDL file IO checks passed'
