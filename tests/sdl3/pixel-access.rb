# get_pixel / set_pixel semantics. Run with and without HiRes.
begin
  def assert_equal(expected, actual, label)
    raise "#{label}: #{actual.inspect} != #{expected.inspect}" unless expected == actual
  end
  def rgba(color)
    [color.red, color.green, color.blue, color.alpha].map(&:round)
  end

  bitmap = Bitmap.new(4, 3)
  bitmap.fill_rect(bitmap.rect, Color.new(10, 20, 30, 255))
  before = bitmap.raw_data

  # Out-of-range writes are ignored and must not touch any pixel or memory.
  [[-1, 0], [0, -1], [4, 0], [0, 3], [4, 3], [-100, 100], [1 << 20, 1]].each do |x, y|
    bitmap.set_pixel(x, y, Color.new(255, 255, 255, 255))
  end
  assert_equal(before, bitmap.raw_data, 'out-of-range set_pixel')
  [[-1, 0], [4, 0], [0, 3]].each do |x, y|
    assert_equal([0, 0, 0, 0], rgba(bitmap.get_pixel(x, y)), "out-of-range get_pixel #{x},#{y}")
  end

  # Channels round to the nearest 8-bit value.
  bitmap.set_pixel(1, 1, Color.new(127.6, 127.4, 0.5, 254.5))
  assert_equal([128, 127, 1, 255], bitmap.raw_data.bytes[(1 * 4 + 1) * 4, 4], 'set_pixel rounding')

  # Round trip, both straight to GPU and through an existing CPU readback cache.
  bitmap.set_pixel(3, 2, Color.new(1, 2, 3, 4))
  assert_equal([1, 2, 3, 4], rgba(bitmap.get_pixel(3, 2)), 'set/get round trip')
  bitmap.get_pixel(0, 0) # populate the CPU cache
  bitmap.set_pixel(2, 0, Color.new(200, 150, 100, 50))
  assert_equal([200, 150, 100, 50], rgba(bitmap.get_pixel(2, 0)), 'set/get through cache')
  assert_equal([10, 20, 30, 255], rgba(bitmap.get_pixel(0, 0)), 'neighbour untouched')

  bitmap.dispose
  File.write('result.txt', 'PASS')
rescue Exception => e
  File.write('result.txt', "FAIL #{e.class}: #{e.message}\n#{e.backtrace.join("\n")}")
end
