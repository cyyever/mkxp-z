# Run with enableHires=true and framebufferScalingFactor=3.
begin
  colors = [[255, 0, 0, 255], [0, 255, 0, 255],
            [0, 0, 255, 255], [255, 255, 255, 255]]
  pattern = Bitmap.new(640, 480)
  colors.each_with_index do |rgba, index|
    pattern.fill_rect((index % 2) * 320, (index / 2) * 240, 320, 240, Color.new(*rgba))
  end
  sprite = Sprite.new
  sprite.bitmap = pattern
  3.times { Graphics.update }
  frame = Graphics.snap_to_bitmap
  raise 'logical snapshot dimensions changed' unless [frame.width, frame.height] == [640, 480]
  raise 'high-resolution snapshot missing' unless frame.hires && [frame.hires.width, frame.hires.height] == [1920, 1440]
  verify = lambda do |bitmap, factor|
    colors.each_with_index do |rgba, index|
      pixel = bitmap.get_pixel(((index % 2) * 320 + 160) * factor,
                               ((index / 2) * 240 + 120) * factor)
      actual = [pixel.red, pixel.green, pixel.blue, pixel.alpha]
      raise "snapshot pixels: #{actual.inspect} != #{rgba.inspect}" unless actual == rgba
    end
  end
  verify.call(frame, 1)
  verify.call(frame.hires, 3)
  frame.to_file('logical.png')
  frame.hires.to_file('hires.png')
  Graphics.screenshot('screen.png')
  [['logical', 1], ['screen', 1], ['hires', 3]].each do |name, factor|
    loaded = Bitmap.new(name)
    raise "#{name} file dimensions" unless [loaded.width, loaded.height] == [640 * factor, 480 * factor]
    verify.call(loaded, factor)
    loaded.dispose
  end
  [frame, sprite, pattern].each(&:dispose)
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
