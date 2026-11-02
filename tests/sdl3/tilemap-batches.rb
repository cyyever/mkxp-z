# Pixel checks for ground/Z-layer ranges, sprite ordering and flash rebuilds.
begin
  tiles = Bitmap.new(256, 32)
  [[255, 0, 0], [0, 255, 0], [0, 0, 255], [0, 0, 0]].each_with_index do |rgb, i|
    tiles.fill_rect(i * 32, 0, 32, 32, Color.new(*rgb))
  end
  map = Table.new(3, 2, 2)
  map[0, 0, 0] = map[1, 0, 0] = 384
  map[0, 0, 1] = map[0, 1, 0] = 385
  map[1, 0, 1] = map[1, 1, 0] = 386
  map[2, 0, 0] = map[2, 1, 0] = 387
  priorities = Table.new(388)
  priorities[385], priorities[386] = 1, 3
  tilemap = Tilemap.new
  tilemap.tileset, tilemap.map_data, tilemap.priorities = tiles, map, priorities
  sprite_image = Bitmap.new(64, 32)
  sprite_image.fill_rect(sprite_image.rect, Color.new(255, 0, 255))
  sprite = Sprite.new
  sprite.bitmap, sprite.z = sprite_image, 96
  snapshot = lambda do
    Graphics.update
    Graphics.snap_to_bitmap
  end
  rgb_at = lambda { |frame, x, y| c = frame.get_pixel(x, y); [c.red, c.green, c.blue] }
  frame = snapshot.call
  raise 'sprite did not split the Z-layer batch' unless rgb_at.call(frame, 16, 16) == [255, 0, 255]
  raise 'high-priority tile lost its range or order' unless rgb_at.call(frame, 48, 16) == [0, 0, 255]
  frame.dispose
  sprite.visible = false
  frame = snapshot.call
  raise 'low-priority tile missing after batches merged' unless rgb_at.call(frame, 16, 16) == [0, 255, 0]
  raise 'later Z-layer missing after batches merged' unless rgb_at.call(frame, 48, 48) == [0, 0, 255]
  frame.dispose

  flash = Table.new(3, 2)
  flash[2, 0] = 0xF00
  tilemap.flash_data = flash
  frame = snapshot.call
  red = rgb_at.call(frame, 80, 16)
  raise "flash geometry missing: #{red.inspect}" unless red[0] > 40 && red[1..2] == [0, 0]
  frame.dispose
  flash[2, 0] = 0
  frame = snapshot.call
  raise 'empty flash rebuild retained stale triangles' unless rgb_at.call(frame, 80, 16) == [0, 0, 0]
  frame.dispose
  flash[2, 0] = 0x00F
  tilemap.ox = 32
  frame = snapshot.call
  blue = rgb_at.call(frame, 48, 16)
  raise "flash scroll/rebuild lost its offset: #{blue.inspect}" unless blue[0..1] == [0, 0] && blue[2] > 40
  frame.dispose
  tilemap.flash_data = nil
  frame = snapshot.call
  raise 'removing flash data retained stale triangles' unless rgb_at.call(frame, 48, 16) == [0, 0, 0]
  [frame, sprite, sprite_image, tilemap, tiles].each(&:dispose)
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
