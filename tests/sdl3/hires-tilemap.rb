# Run with enableHires=true and framebufferScalingFactor=textureScalingFactor=3.
# Detail that exists only in the HiRes children of a Tilemap's autotile and
# tileset must reach the physical frame instead of being reduced to the
# logical atlas resolution.
begin
  red = Color.new(255, 0, 0, 255)
  green = Color.new(0, 255, 0, 255)
  # One-physical-pixel vertical stripes: a third green, two thirds red.
  stripe = lambda do |bitmap|
    hires = bitmap.hires
    raise 'HiRes child missing' unless hires && hires.width == bitmap.width * 3
    hires.fill_rect(0, 0, hires.width, hires.height, red)
    (0...hires.width).step(3) { |x| hires.fill_rect(x, 0, 1, hires.height, green) }
  end

  autotile = Bitmap.new(96, 128)
  stripe.call(autotile)
  tileset = Bitmap.new(256, 64)
  stripe.call(tileset)
  # An animated autotile drawn through the logical API: four solid frames.
  animated = Bitmap.new(384, 128)
  frame_colors = [[255, 0, 0], [0, 255, 0], [0, 0, 255], [255, 255, 0]]
  frame_colors.each_with_index do |rgb, i|
    animated.fill_rect(96 * i, 0, 96, 128, Color.new(*rgb, 255))
  end

  data = Table.new(20, 15, 3)
  data[1, 1, 0] = 48   # autotile 0, pattern 0
  data[4, 1, 0] = 384  # first tileset tile
  data[7, 1, 0] = 96   # autotile 1, pattern 0
  tilemap = Tilemap.new
  tilemap.tileset = tileset
  tilemap.autotiles[0] = autotile
  tilemap.autotiles[1] = animated
  tilemap.map_data = data
  tilemap.priorities = Table.new(384 + 16)
  3.times { Graphics.update }

  frame = Graphics.snap_to_bitmap
  physical = frame.hires
  raise 'high-resolution snapshot missing' unless physical && physical.width == 1920

  # Count pure green and pure red physical pixels inside one 32x32 tile.
  measure = lambda do |tx, ty|
    counts = Hash.new(0)
    ((ty * 32 + 2) * 3...(ty * 32 + 30) * 3).each do |y|
      ((tx * 32 + 2) * 3...(tx * 32 + 30) * 3).each do |x|
        c = physical.get_pixel(x, y)
        key = [c.red, c.green, c.blue, c.alpha]
        counts[:green] += 1 if key == [0, 255, 0, 255]
        counts[:red] += 1 if key == [255, 0, 0, 255]
        counts[:total] += 1
      end
    end
    counts
  end
  { 'autotile' => [1, 1], 'tileset' => [4, 1] }.each do |name, (tx, ty)|
    counts = measure.call(tx, ty)
    green_share = counts[:green].to_f / counts[:total]
    red_share = counts[:red].to_f / counts[:total]
    unless (0.25..0.42).cover?(green_share) && (0.58..0.75).cover?(red_share)
      raise "#{name} lost HiRes detail: green=#{green_share.round(3)} red=#{red_share.round(3)}"
    end
  end

  # The animated autotile must show one of its frames, identically in the
  # logical and physical snapshots.
  logical = frame.get_pixel(7 * 32 + 16, 1 * 32 + 16)
  physical_pixel = physical.get_pixel((7 * 32 + 16) * 3, (1 * 32 + 16) * 3)
  rgb = [logical.red, logical.green, logical.blue]
  raise "animated autotile frame: #{rgb.inspect}" unless frame_colors.include?(rgb)
  unless [physical_pixel.red, physical_pixel.green, physical_pixel.blue] == rgb
    raise "animated autotile physical #{[physical_pixel.red, physical_pixel.green, physical_pixel.blue].inspect} != #{rgb.inspect}"
  end

  [frame, tilemap, autotile, animated, tileset].each(&:dispose)
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
