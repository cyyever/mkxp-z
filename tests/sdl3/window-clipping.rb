# Sliding windows may be outside the framebuffer or smaller than their padding.
# They must draw only their visible contents and restore the following draw's clip.
begin
  Graphics.resize_screen(128, 96)
  skin = Bitmap.new(192, 128)
  skin.clear
  contents = Bitmap.new(96, 48)
  contents.fill_rect(contents.rect, Color.new(180, 50, 20, 255))
  window = Window.new
  window.windowskin = skin
  window.contents = contents
  window.width = 128
  window.height = 80
  window.z = 100
  marker = Sprite.new
  marker.bitmap = Bitmap.new(4, 4)
  marker.bitmap.fill_rect(marker.bitmap.rect, Color.new(0, 255, 0, 255))
  marker.z = 200

  def rgb(image, x, y)
    color = image.get_pixel(x, y)
    [color.red, color.green, color.blue].map(&:round)
  end

  [[160, 8], [-160, 8], [8, 128], [8, -128],
   [-80, 8], [80, 8], [8, -32], [8, 64], [8, 8]].each do |x, y|
    window.x, window.y = x, y
    image = Graphics.snap_to_bitmap
    96.times do |py|
      128.times do |px|
        expected = if px < 4 && py < 4
                     [0, 255, 0]
                   elsif px >= x + 16 && px < x + 112 && py >= y + 16 && py < y + 64
                     [180, 50, 20]
                   else
                     [0, 0, 0]
                   end
        actual = rgb(image, px, py)
        raise "clip #{x},#{y} pixel #{px},#{py}: #{actual} != #{expected}" unless actual == expected
      end
    end
    image.dispose
  end

  window.x, window.y = 40, 20
  [[0, 0], [16, 16], [32, 32]].each do |width, height|
    window.width, window.height = width, height
    image = Graphics.snap_to_bitmap
    raise 'empty content area drew pixels' unless rgb(image, 56, 36) == [0, 0, 0]
    raise 'empty clip leaked to next sprite' unless rgb(image, 1, 1) == [0, 255, 0]
    image.dispose
  end
  File.write('result.txt', 'PASS')
rescue Exception => error
  File.write('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
