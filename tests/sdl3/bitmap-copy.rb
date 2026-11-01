# Run with the native fixture launcher and --no-hires.
begin
  def verify(expected, bitmap, label)
    raise "#{label}: raw RGBA changed" unless bitmap.raw_data == expected
  end
  size = 16
  bytes = (0...size * size).flat_map do |index|
    [(index * 37) % 256, (index * 71) % 256, (index * 13) % 256, [0, 64, 128, 255][index % 4]]
  end.pack('C*')
  unread_source = Bitmap.new(size, size)
  unread_source.raw_data = bytes
  unread_copy = unread_source.dup
  chained_copy = unread_copy.dup
  unread_source.dispose
  unread_copy.dispose
  reused_before_read = Bitmap.new(size, size)
  reused_before_read.fill_rect(0, 0, size, size, Color.new(0, 255, 0, 255))
  GC.start
  verify(bytes, chained_copy, 'unread copy chain after source disposal/reuse')
  [chained_copy, reused_before_read].each(&:dispose)
  original = Bitmap.new(size, size)
  original.raw_data = bytes
  copy = original.dup
  verify(bytes, copy, 'transparent RGB clone')
  original.raw_data = "\xff\0\0\xff".b * (size * size)
  original.dispose
  recycled = Bitmap.new(size, size)
  recycled.fill_rect(0, 0, size, size, Color.new(0, 255, 0, 255))
  verify(bytes, copy, 'clone after source mutation/disposal/reuse')
  # Queue a draw from a clone before its source object is disposed.
  drawn = Bitmap.new(size, size)
  drawn.blt(0, 0, copy, copy.rect)
  copy.dispose
  GC.start
  verify(bytes, drawn, 'copy then queued draw')
  # The clone must see pending GPU writes, not the texture's earlier contents.
  queued = Bitmap.new(size, size)
  queued.fill_rect(0, 0, size, size, Color.new(11, 22, 33, 255))
  queued_copy = queued.dup
  queued.fill_rect(0, 0, size, size, Color.new(44, 55, 66, 255))
  queued.dispose
  verify([11, 22, 33, 255].pack('C4') * (size * size), queued_copy, 'pending write before copy')
  queued_copy.to_file('copy.png')
  loaded = Bitmap.new('copy')
  verify(queued_copy.raw_data, loaded, 'clone PNG')
  [loaded, queued_copy, drawn, recycled].each(&:dispose)
  File.binwrite('result.txt', 'PASS')
rescue Exception => error
  File.binwrite('result.txt', "FAIL #{error.class}: #{error.message}\n#{error.backtrace.join("\n")}")
end
