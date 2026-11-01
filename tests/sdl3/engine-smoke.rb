begin
  image = Bitmap.new('icon')
  raise 'bundled PNG did not load' unless image.width > 0 && image.height > 0
  image.dispose

  bitmap = Bitmap.new(180, 40)
  bitmap.fill_rect(0, 0, 180, 40, Color.new(120, 50, 20))
  bitmap.font = Font.new('Liberation Sans', 18).dup
  bitmap.draw_text(0, 0, 180, 40, "UTF-8 \u00e9")
  bitmap.to_file('generated.png')
  loaded = Bitmap.new('generated')
  raise 'new cached PNG did not load' unless loaded.width == 180 && loaded.height == 40
  loaded.dispose
  bitmap.dispose
  GC.start

  if RUBY_PLATFORM.include?('mswin') || RUBY_PLATFORM.include?('mingw')
    pid = MiniFFI.new('kernel32.dll', 'GetCurrentProcessId', [], 'L').call
    raise 'native function pointer was truncated' unless pid > 0
  end
  expected = { 'text' => "\u96e8\u8840\0ok", 'integer' => 42, 'array' => [true, nil, 1.5] }
  CFG['migration_smoke'] = expected
  raise 'JSON settings roundtrip' unless CFG['migration_smoke'] == expected

  Graphics.resize_screen(800, 600)
  raise 'high-resolution resize failed' unless Graphics.width == 800 && Graphics.height == 600
  3.times { Graphics.update }
  Graphics.screenshot('screen.png')
  screenshot = Bitmap.new('screen')
  raise 'screenshot dimensions' unless screenshot.width == 800 && screenshot.height == 600
  screenshot.dispose
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
