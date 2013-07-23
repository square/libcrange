#!/usr/bin/ruby

$:.push '../ruby'

require 'rangelib'
require 'test/unit'


class TestRangeLib < Test::Unit::TestCase
 
  def test_simple
    r = RangeLib.new()
    assert_equal((100..200).to_a.map {|x| "bar" + x.to_s}, r.expand("bar100..200").sort)
    assert_equal("bar100..201", r.eval("bar100..200,bar201"))
    r.destroy
  end
 
end
