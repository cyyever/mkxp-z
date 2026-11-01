begin
  image = Bitmap.new('icon')
  raise 'bundled PNG did not load' unless image.width > 0 && image.height > 0
  image.dispose

  bitmap = Bitmap.new(180, 40)
  bitmap.fill_rect(0, 0, 180, 40, Color.new(120, 50, 20))
  bitmap.font = Font.new('Liberation Sans', 18).dup
  bitmap.draw_text(0, 0, 180, 40, "UTF-8 \u00e9")
  %w[unsupported.jpg unsupported.bmp].each do |filename|
    begin
      bitmap.to_file(filename)
      raise 'non-PNG output unexpectedly accepted'
    rescue ArgumentError
      raise 'rejected output created a file' if File.exist?(filename)
    end
  end
  bitmap.to_file('generated.png')
  bitmap.to_file("\u96e8\u8840-\u00e9.png")
  unicode_image = Bitmap.new("\u96e8\u8840-\u00e9")
  pixel = unicode_image.get_pixel(179, 39)
  raise 'UTF-8 PNG pixel mismatch' unless [pixel.red, pixel.green, pixel.blue, pixel.alpha] == [120, 50, 20, 255]
  unicode_image.dispose
  loaded = Bitmap.new('generated')
  raise 'new cached PNG did not load' unless loaded.width == 180 && loaded.height == 40
  loaded.dispose
  bitmap.dispose
  GC.start

  pattern = Bitmap.new(32, 32)
  pattern.fill_rect(0, 0, 16, 16, Color.new(255, 0, 0))
  pattern.fill_rect(16, 0, 16, 16, Color.new(0, 255, 0))
  pattern.fill_rect(0, 16, 16, 16, Color.new(0, 0, 255))
  pattern.fill_rect(16, 16, 16, 16, Color.new(255, 255, 255))
  sprite = Sprite.new
  sprite.bitmap = pattern

  if RUBY_PLATFORM.include?('mswin')
    pid = MiniFFI.new('kernel32.dll', 'GetCurrentProcessId', [], 'L').call
    raise 'native function pointer was truncated' unless pid > 0
  end
  [:bgm_play, :bgs_play, :se_play].each do |operation|
    failed = false
    begin
      Audio.send(operation, 'invalid')
    rescue SDLError
      failed = true
    end
    raise "#{operation} silently accepted malformed audio" unless failed
  end
  Audio.bgm_play('tone', 75, 100, 0.25)
  5.times { Graphics.update }
  raise 'BGM seek position was lost' unless Audio.bgm_pos >= 0.25
  Audio.bgs_play('tone-mono', 60, 120)
  Audio.se_play('tone', 80, 90)
  3.times { Graphics.update }
  Audio.bgm_stop
  Audio.bgm_play('tone-mono', 80, 100)
  3.times { Graphics.update }
  Audio.bgm_stop
  Audio.bgs_stop
  Audio.se_stop
  Audio.se_play('tone-mono', 100, 100)
  3.times { Graphics.update }
  Audio.se_stop
  expected = { 'text' => "\u96e8\u8840\0ok", 'integer' => 42, 'array' => [true, nil, 1.5] }
  CFG['migration_smoke'] = expected
  raise 'JSON settings roundtrip' unless CFG['migration_smoke'] == expected

  [[1280, 720], [640, 480], [800, 600]].each do |width, height|
    Graphics.resize_screen(width, height)
    raise 'repeated resolution change failed' unless Graphics.width == width && Graphics.height == height
    3.times { Graphics.update }
    frame = Graphics.snap_to_bitmap
    [[8, 8, [255, 0, 0]], [24, 8, [0, 255, 0]],
     [8, 24, [0, 0, 255]], [24, 24, [255, 255, 255]]].each do |x, y, expected|
      actual = frame.get_pixel(x, y)
      raise 'rendered channels or orientation changed' unless [actual.red, actual.green, actual.blue] == expected
    end
    frame.dispose
  end
  Graphics.screenshot('screen.png')
  screenshot = Bitmap.new('screen')
  raise 'screenshot dimensions' unless screenshot.width == 800 && screenshot.height == 600
  screenshot.dispose
  sprite.dispose
  pattern.dispose
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
