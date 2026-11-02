begin
  def assert_equal(expected, actual, label)
    raise "#{label}: #{actual.inspect} != #{expected.inspect}" unless expected == actual
  end
  def check(bitmap, rgba, label)
    assert_equal(rgba, bitmap.raw_data.bytes.first(4), "#{label} raw")
    bitmap.to_file('readback.png')
    image = Bitmap.new('readback')
    assert_equal([bitmap.width, bitmap.height], [image.width, image.height], "#{label} dimensions")
    assert_equal(rgba, image.raw_data.bytes.first(4), "#{label} PNG")
    image.dispose
  end
  source = Bitmap.new(2, 2)
  source.fill_rect(0, 0, 2, 2, Color.new(255, 0, 0, 255))
  target = Bitmap.new(4, 4)
  target.stretch_blt(Rect.new(0, 0, 4, 4), source, source.rect)
  check(target, [255, 0, 0, 255], 'stretch')
  # Re-read after direct child writes: the parent's cached CPU surface must expire.
  target.hires.fill_rect(0, 0, 12, 12, Color.new(10, 20, 30, 128))
  check(target, [10, 20, 30, 128], 'child mutation')
  target.hires.clear
  check(target, [0, 0, 0, 0], 'clear alpha')
  # Averaging a fully transparent HiRes block must not divide by zero (NaN).
  pixel = target.get_pixel(1, 1)
  assert_equal([0.0, 0.0, 0.0, 0.0], [pixel.red, pixel.green, pixel.blue, pixel.alpha], 'transparent get_pixel')
  pixel = target.get_pixel(target.width, 0)
  assert_equal([0.0, 0.0, 0.0, 0.0], [pixel.red, pixel.green, pixel.blue, pixel.alpha], 'out-of-range get_pixel')
  # dup/clone must carry the HiRes companion, not just the logical texture.
  painted = Bitmap.new(4, 4)
  painted.hires.fill_rect(0, 0, 12, 12, Color.new(40, 50, 60, 255))
  painted.hires.fill_rect(1, 1, 1, 1, Color.new(200, 100, 50, 255)) # detail only HiRes can hold
  copy = painted.dup
  raise 'dup lost its HiRes companion' unless copy.hires
  # Ruby's dup copies the @hires ivar; it must be replaced by the copy's own.
  raise 'dup shares the original HiRes bitmap' if copy.hires.equal?(painted.hires)
  original = painted.hires.raw_data
  assert_equal(original, copy.hires.raw_data, 'dup HiRes pixels')
  copy.hires.fill_rect(0, 0, 12, 12, Color.new(1, 2, 3, 255))
  assert_equal(original, painted.hires.raw_data, 'original HiRes untouched by copy')
  assert_equal([1, 2, 3, 255], copy.raw_data.bytes.first(4), 'copy reads its own HiRes')
  copy.dispose
  painted.dispose
  target.hires.fill_rect(0, 0, 12, 12, Color.new(255, 0, 0, 255))
  target.hue_change(120)
  check(target, [0, 255, 0, 255], 'hue')
  fresh = Bitmap.new(4, 4)
  fresh.raw_data = [7, 8, 9, 64].pack('C4') * 16
  check(fresh, [7, 8, 9, 64], 'raw replacement before read')
  fresh.raw_data = [17, 18, 19, 128].pack('C4') * 16
  check(fresh, [17, 18, 19, 128], 'raw replacement after read')
  fresh.hires.fill_rect(0, 0, 12, 12, Color.new(27, 28, 29, 255))
  check(fresh, [27, 28, 29, 255], 'child supersedes logical raw cache')
  fresh.dispose
  old = target.hires
  replacement = Bitmap.new(12, 12).hires
  replacement.fill_rect(0, 0, replacement.width, replacement.height, Color.new(0, 0, 255, 255))
  target.hires = replacement
  check(target, [0, 0, 255, 255], 'replacement')
  old.fill_rect(0, 0, old.width, old.height, Color.new(255, 255, 0, 255))
  check(target, [0, 0, 255, 255], 'detached child')
  text = Bitmap.new(180, 40)
  before = text.raw_data
  text.draw_text(0, 0, 180, 40, 'HiRes')
  raise 'text CPU readback stale' if text.raw_data == before
  text.to_file('text.png')
  loaded = Bitmap.new('text')
  assert_equal(text.raw_data, loaded.raw_data, 'text PNG')
  loaded.dispose
  begin
    replacement.hires = target
    raise 'cyclic replacement succeeded'
  rescue ArgumentError
  end
  target.hires.dispose
  begin
    target.raw_data
    raise 'disposed child read succeeded'
  rescue RGSSError
  end
  old.dispose
  [text, target, source].each(&:dispose)
  32.times do
    temporary = Bitmap.new(16, 16)
    temporary.hires.fill_rect(0, 0, 48, 48, Color.new(1, 2, 3, 255))
    assert_equal([1, 2, 3, 255], temporary.raw_data.bytes.first(4), 'repeated read/dispose')
    temporary.dispose
  end
  # A pooled logical texture must not retain its previous bitmap's HiRes child.
  pooled = Bitmap.new(16, 16)
  pooled.fill_rect(0, 0, 16, 16, Color.new(17, 33, 65, 255))
  pooled.to_file('pooled.png')
  pooled.dispose
  plain = Bitmap.new('pooled')
  raise 'plain PNG acquired a HiRes replacement' if plain.hires
  copied = plain.dup
  assert_equal([17, 33, 65, 255], copied.raw_data.bytes.first(4), 'pooled PNG clone')
  destination = Bitmap.new(8, 8)
  destination.stretch_blt(destination.rect, plain, plain.rect)
  assert_equal([17, 33, 65, 255], destination.raw_data.bytes.first(4), 'pooled PNG blit')
  [destination, copied, plain].each(&:dispose)
  # Menu previews are captured after the map sprites have been disposed.
  scene = Bitmap.new(16, 16)
  scene.fill_rect(scene.rect, Color.new(41, 82, 123, 255))
  sprite = Sprite.new
  sprite.bitmap = scene
  Graphics.update
  Graphics.freeze
  sprite.dispose
  scene.dispose
  snapshot = Graphics.snap_to_bitmap
  assert_equal([41, 82, 123, 255], snapshot.raw_data.bytes.first(4), 'frozen snapshot')
  snapshot.dispose
  Graphics.screenshot('frozen.png')
  preview = Bitmap.new('frozen')
  assert_equal([41, 82, 123, 255], preview.raw_data.bytes.first(4), 'frozen PNG preview')
  preview.dispose
  Graphics.transition(1)
  GC.start
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
