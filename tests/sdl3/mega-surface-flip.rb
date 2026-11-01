# Run as customScript with enableHires=false. No game assets or saves are used.
begin
  source = Bitmap.new(4, 4)
  source.raw_data = (0...16).flat_map { |index| [index + 1, 50, 100, 255] }.pack('C*')
  target = Bitmap.new(Bitmap.max_size + 1, 8)
  raise 'fixture must exercise the mega-surface path' unless target.mega?
  cases = [
    ['horizontal crop', Rect.new(5, 1, -2, 2), Rect.new(1, 1, 2, 2), 3, 1, [[7, 6], [11, 10]]],
    ['vertical at positive y', Rect.new(1, 4, 2, -2), Rect.new(1, 1, 2, 2), 1, 2, [[10, 11], [6, 7]]],
    ['both axes', Rect.new(5, 4, -2, -2), Rect.new(1, 1, 2, 2), 3, 2, [[11, 10], [7, 6]]],
    ['negative y does not flip vertically', Rect.new(5, -1, -2, 3), Rect.new(0, 0, 2, 3), 3, 0, [[6, 5], [10, 9]]],
    ['nearest scaling', Rect.new(5, 0, -4, 4), Rect.new(1, 1, 2, 2), 1, 0,
     [[7, 7, 6, 6], [7, 7, 6, 6], [11, 11, 10, 10], [11, 11, 10, 10]]]
  ]
  cases.each do |label, destination, crop, x, y, expected|
    target.clear
    target.stretch_blt(destination, source, crop)
    expected.each_with_index do |row, dy|
      row.each_with_index do |red, dx|
        pixel = target.get_pixel(x + dx, y + dy)
        actual = [pixel.red, pixel.green, pixel.blue, pixel.alpha]
        raise "#{label} at #{dx},#{dy}: #{actual.inspect}" unless actual == [red, 50, 100, 255]
      end
    end
  end
  # Copy/flip must preserve straight RGBA before the existing opacity blend.
  source.raw_data = (0...16).flat_map { |index| [index * 10, 70, 140, 64 + (index % 3) * 64] }.pack('C*')
  [255, 117].each do |opacity|
    target.clear
    target.fill_rect(0, 0, 8, 8, Color.new(20, 30, 40, 200))
    target.stretch_blt(Rect.new(0, 0, 2, 2), source, Rect.new(1, 1, 2, 2), opacity)
    reference = 2.times.map { |y| 2.times.map { |x| target.get_pixel(x, y) } }
    target.stretch_blt(Rect.new(6, 0, -2, 2), source, Rect.new(1, 1, 2, 2), opacity)
    2.times do |y|
      2.times do |x|
        actual = target.get_pixel(4 + x, y)
        expected = reference[y][1 - x]
        components = ->(color) { [color.red, color.green, color.blue, color.alpha] }
        raise "opacity #{opacity} RGBA changed" unless components.call(actual) == components.call(expected)
      end
    end
  end
  [target, source].each(&:dispose)
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
