# This can also run against the standalone native test extension.
require ARGV.shift if ARGV.first

def check(value, message)
  raise message unless value
end

def expect_error(klass)
  begin
    yield
  rescue klass
    check(json_settings_active_scopes == 0, 'Ruby exception skipped C++ cleanup')
    return
  end
  raise "expected #{klass}"
end

input = {"雨血\0键" => [nil, true, false, -2147483648, 2147483647, 1, 1.25, "中文\0tail"],
         'nested' => {'empty_array' => [], 'empty_hash' => {}, 'empty_string' => ''}}
result = json_settings_round_trip(input)
check(result == input, 'nested JSON/Ruby round trip changed values')
check(json_settings_serialized_round_trip(input) == input, 'saved JSON text round trip changed values')
check(result["雨血\0键"][5].is_a?(Integer), 'JSON integer became Float')
check(result["雨血\0键"][6].is_a?(Float), 'JSON float became Integer')
check(result["雨血\0键"][7].encoding == Encoding::UTF_8, 'JSON text not UTF-8')
check(json_settings_parse("{ // JSON5\n title: '雨血', values: [1, 1.25, true, null,], hex: 0x10, }") ==
      {'title' => '雨血', 'values' => [1, 1.25, true, nil], 'hex' => 16}, 'JSON5 compatibility')
check(json_settings_parse('{"key\\u0000tail":"雨血\\u0000text"}') ==
      {"key\0tail" => "雨血\0text"}, 'escaped NUL truncated')
expect_error(ArgumentError) { json_settings_parse('{bad') }
expect_error(TypeError) { json_settings_round_trip([Object.new]) }
expect_error(TypeError) { json_settings_round_trip({:symbol => 1}) }
expect_error(RangeError) { json_settings_round_trip([2**40]) }
expect_error(RangeError) { json_settings_round_trip([2**100]) }
array = []; array << array
expect_error(TypeError) { json_settings_round_trip(array) }
hash = {}; hash['cycle'] = hash
expect_error(TypeError) { json_settings_round_trip(hash) }
shared = [1]
check(json_settings_round_trip([shared, shared]) == [[1], [1]], 'shared non-cyclic input rejected')
key = Object.new
def key.to_str = raise('key coercion failed')
expect_error(RuntimeError) { json_settings_round_trip({'before' => [1, 2], key => 3}) }

original_stress = GC.stress
begin
  GC.stress = true
  3.times { check(json_settings_round_trip(input) == input, 'GC lost nested JSON/Ruby values') }
ensure
  GC.stress = original_stress
end
puts 'JSON settings conversion checks passed'
