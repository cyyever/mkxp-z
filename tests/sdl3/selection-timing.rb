# Selection feedback retains its original cycle at normal and accelerated rates.
begin
  Graphics.resize_screen(640, 480)
  skin = Bitmap.new(192, 128)
  skin.fill_rect(skin.rect, Color.new(0, 0, 0, 255))
  skin.fill_rect(128, 64, 32, 32, Color.new(255, 255, 255, 255))
  window = Window.new
  window.x = 64
  window.y = 64
  window.width = 160
  window.height = 96
  window.windowskin = skin
  window.cursor_rect.set(8, 8, 48, 32)
  sprite = RPG::Sprite.new
  sprite.bitmap = Bitmap.new(16, 16)
  sprite.bitmap.fill_rect(sprite.bitmap.rect, Color.new(80, 20, 20, 255))
  sprite.x = 320
  sprite.y = 200

  [40, 120].each do |rate|
    Graphics.frame_rate = rate
    window.active = false
    window.active = true
    sprite.blink_off
    sprite.blink_on
    started = System.uptime
    troughs = [[], []]
    low = [false, false]
    levels = [[], []]
    while System.uptime - started < 2.25
      window.update
      sprite.update
      Graphics.update
      image = Graphics.snap_to_bitmap
      values = [image.get_pixel(90, 90).red, sprite.color.alpha]
      image.dispose
      values.each_with_index do |value, index|
        levels[index] << value
        below = value <= (index == 0 ? 144 : 12)
        troughs[index] << System.uptime if below && !low[index]
        low[index] = below
      end
    end
    troughs.each_with_index do |times, index|
      raise "Missing selection animation #{index} at #{rate} FPS" if times.size < 2
      times.each_cons(2) do |a, b|
        raise "Accelerated selection cycle #{index}: #{b - a}s at #{rate} FPS" unless (0.65..0.95).cover?(b - a)
      end
      raise 'Selection effect disappeared' unless levels[index].max - levels[index].min > 60
    end
    raise 'Game speed changed' unless Graphics.frame_rate == rate
  end

  sprite.blink_off
  raise 'Deselection did not clear highlight' unless sprite.color.alpha == 0
  sprite.whiten
  sprite.update
  raise 'Action whitening removed' unless sprite.color.alpha > 0
  sprite.color.set(0, 0, 0, 0)
  sprite.flash(Color.new(255, 255, 255, 255), 8)
  sprite.update
  image = Graphics.snap_to_bitmap
  raise 'Action flash removed' unless image.get_pixel(325, 205).red > 80
  image.dispose
  sprite.flash(nil, 8)
  sprite.update
  image = Graphics.snap_to_bitmap
  raise 'Intentional sprite hiding changed' unless image.get_pixel(325, 205).red == 0
  image.dispose
  File.write('result.txt', 'PASS')
rescue Exception => e
  File.write('result.txt', "FAIL #{e.class}: #{e.message}\n#{e.backtrace.join("\n")}")
end
