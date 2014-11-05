#!/usr/bin/ruby
# transform yamlfile range datadir into sqlite
# usage $0 --rangedata /path/to/yamfiles --module_path /usr/lib/libcrange 

require 'rubygems'

require 'sqlite3'
require 'yaml'
require 'getoptlong'
require 'tempfile'
require 'ffi'

opts = GetoptLong.new(['--rangedata', GetoptLong::REQUIRED_ARGUMENT],
                      ['--module_path', GetoptLong::REQUIRED_ARGUMENT],
                      ['--sqlite_db', GetoptLong::REQUIRED_ARGUMENT],
                      ['--help', '-h', GetoptLong::NO_ARGUMENT])

rangedata_path = "" # range data dir
module_path = ""    # libcrange plugin dir
debug = false
sqlite_db = nil
opts.each do |opt, arg|
  case opt
  when '--rangedata'
    rangedata_path = arg
  when '--sqlite_db'
    sqlite_db = arg
  when '--module_path'
    module_path = arg
  when '--debug'
    debug = arg
  end
end

raise "required: --sqlite_db" unless sqlite_db


## http://www.dzone.com/snippets/getting-array-strings-char-ffi
class FFI::Pointer
  def read_array_of_string
    elements = []
    loc = self
    until ((element = loc.read_pointer).null?)
      elements << element.read_string
      loc += FFI::Type::POINTER.size
    end
    return elements
  end
end

class RangeLib
  extend FFI::Library

  arch, os = RUBY_PLATFORM.split('-')
  if os =~ /^darwin/
    ffi_lib "libcrange.dylib"
  else
    ffi_lib "libcrange.so"
  end

  attach_function 'range_easy_create', [:string], :pointer
  attach_function 'range_easy_expand', [:pointer, :string], :pointer
  attach_function 'range_easy_eval', [:pointer, :string], :string
  attach_function 'range_easy_compress', [:pointer, :string], :string
  attach_function 'range_easy_destroy', [:pointer], :int
  
  def initialize(conf=nil)
    @elr = RangeLib.range_easy_create(nil)
  end

  def expand(range_exp)
    return RangeLib.range_easy_expand(@elr, range_exp).read_array_of_string
  end
  def eval(range_exp)
    return RangeLib.range_easy_eval(@elr, range_exp)
  end
  def compress(nodes)
    # FIXME
  end
  def destroy
    return RangeLib.range_easy_destroy(@elr)
  end
end



sqlite_filename = sqlite_db
sqlite_filename_tmp = "sqlite_db" + ".tmp"

range_conf_tempfile = Tempfile.new('range.conf')
range_conf_path = range_conf_tempfile.path

range_conf_data = %{
yaml_path=#{rangedata_path}
loadmodule #{module_path}/yamlfile
}

File.open(range_conf_path, "w") do |f|
	f.puts range_conf_data
end

elr = RangeLib.range_easy_create(range_conf_path)
range_conf_tempfile.unlink

all_clusters =  RangeLib.range_easy_expand(elr, "allclusters()").read_array_of_string

File.unlink sqlite_filename_tmp rescue true
db = SQLite3::Database.new sqlite_filename_tmp

db.execute "PRAGMA journal_mode = OFF;"
db.execute "PRAGMA synchronous = OFF;"
# the forward map, using range expr
db.execute "create table clusters (key varchar, value varchar, cluster varchar);"

# hack around has() and so on needing to query individual value elements
# it'd be better to use this instead of clusters entirely -- this is just a hack
db.execute "create table clusters_norange (key varchar, value varchar, cluster varchar);"

# node to containing clusters. we only use key=CLUSTER
db.execute "create table expanded_reverse_clusters (node varchar, key varchar, cluster varchar, warnings varchar);" 


all_clusters.each do |cluster| 
  percluster_time = Time.now
  yaml = YAML.load_file("#{rangedata_path}/#{cluster}.yaml")
  yamlload_time = Time.now
  puts "   yaml load time: #{yamlload_time - percluster_time}" if debug
  yaml.each_pair do |k,v|
    insert_val = v
    if v.kind_of?(Array)
      insert_val = v.join ","
      v.each do |individual_value|
        puts "inserting clusters_norange: '[k, v, cluster]' = #{[k, individual_value, cluster].inspect}" if debug
        db.execute "insert into clusters_norange (key, value, cluster) values (?, ?, ?)", [k, individual_value, cluster]
      end
    else
      puts "inserting clusters_norange: '[k, v, cluster]' = #{[k, insert_val, cluster].inspect}" if debug
      db.execute "insert into clusters_norange (key, value, cluster) values (?, ?, ?)", [k, insert_val, cluster]
    end
    puts "inserting for clusters: '[k, v, cluster]' = #{[k, insert_val, cluster].inspect}" if debug
    db.execute "insert into clusters (key, value, cluster) values (?, ?, ?)", [k, insert_val, cluster]
  end
  forwardmap_time =  Time.now
  puts "   forward-map time: #{forwardmap_time - yamlload_time}" if debug
  keys = RangeLib.range_easy_expand(elr, "%#{cluster}:KEYS").read_array_of_string
  clusterkeys_time =  Time.now
  puts "   clusterkeys time: #{clusterkeys_time - forwardmap_time}" if debug
  keys.each do |key|
    values = RangeLib.range_easy_expand(elr, "%#{cluster}:#{key}").read_array_of_string
    values.each do |val|
      db.execute "insert into expanded_reverse_clusters (node, key, cluster) values (?, ?, ?)", [val, key, cluster]
    end
  end
  allkeys_time =  Time.now
  puts "     allkeys time: #{allkeys_time - clusterkeys_time}" if debug
  puts "cluster #{cluster} took #{Time.now - percluster_time} to process" if debug
end

db.close

File.rename sqlite_filename_tmp, sqlite_filename


