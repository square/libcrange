#!/usr/bin/ruby
# transform yamlfile range datadir into sqlite
# usage $0 --rangedata /path/to/yamfiles --module_path /usr/lib/libcrange 

require 'rubygems'

require 'sqlite3'
require 'yaml'
require 'getoptlong'
require 'tempfile'
require 'ffi'

require 'mdbm'

opts = GetoptLong.new(['--rangedata', GetoptLong::REQUIRED_ARGUMENT],
                      ['--module_path', GetoptLong::REQUIRED_ARGUMENT],
                      ['--sqlite_db', GetoptLong::REQUIRED_ARGUMENT],
                      ['--mdbm', GetoptLong::REQUIRED_ARGUMENT],
                      ['--help', '-h', GetoptLong::NO_ARGUMENT])

rangedata_path = "" # range data dir
module_path = ""    # libcrange plugin dir
mdbm_path = "" # mdbm path
debug = false
sqlite_db = nil
opts.each do |opt, arg|
  case opt
  when '--rangedata'
    rangedata_path = arg
  when '--sqlite_db'
    sqlite_db = arg
  when '--mdbm'
    mdbm_path = arg
  when '--module_path'
    module_path = arg
  when '--debug'
    debug = arg
  end
end

raise "required: --sqlite_db" unless sqlite_db
raise "required: --mdbm" unless mdbm_path


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
  attach_function 'range_easy_expand_as_packed_set', [:pointer, :pointer], :pointer
  
  def initialize(conf=nil)
    @elr = RangeLib.range_easy_create(nil)
  end

  def expand(range_exp)
    return RangeLib.range_easy_expand(@elr, range_exp).read_array_of_string
  end
  def expand_as_packed_set(range_exp)
    return RangeLib.range_easy_expand_as_packed_set(@elr, range_exp)
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

# start build
#
#  page size, presize(initial size)
mdbm_path_tmp = mdbm_path + ".tmp"
File.unlink mdbm_path_tmp rescue true
$mdbm = Mdbm.new(mdbm_path_tmp, (Mdbm::MDBM_O_RDWR|Mdbm::MDBM_O_CREAT), 0644, 2**20, 2**28)


sqlite_filename = sqlite_db
sqlite_filename_tmp = sqlite_db + ".tmp"

range_conf_tempfile = Tempfile.new('range.conf')
range_conf_path = range_conf_tempfile.path

range_conf_data = %{
yaml_path=#{rangedata_path}
loadmodule #{module_path}/yamlfile
}

File.open(range_conf_path, "w") do |f|
	f.puts range_conf_data
end

$elr = RangeLib.range_easy_create(range_conf_path)
range_conf_tempfile.unlink

all_clusters =  RangeLib.range_easy_expand($elr, "allclusters()").read_array_of_string

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

def mdbm_store(cluster, key, val)
  # range_easy_expand_as_packed_set doesn't handle cluster not found, so prune for it and return early if nothing there
  # this is a dumb way to do it, but whatever
  stuff = RangeLib.range_easy_expand($elr, "%{#{cluster.encode("UTF-8")}:#{key.encode("UTF-8")}}").read_array_of_string
  return if stuff.empty?

  packed_set = RangeLib.range_easy_expand_as_packed_set($elr, "%{#{cluster.encode("UTF-8")}:#{key.encode("UTF-8")}}")
  mdbm_key = "#{cluster.encode("UTF-8")}:#{key.encode("UTF-8")}"
  length = packed_set.read_int
  val = packed_set.read_bytes(length)
  ret = $mdbm.store(mdbm_key, val, Mdbm::MDBM_INSERT)
end

all_clusters.each do |cluster| 
  percluster_time = Time.now
  yaml = YAML.load_file("#{rangedata_path}/#{cluster}.yaml")
  yamlload_time = Time.now
  puts "   yaml load time: #{yamlload_time - percluster_time}" if debug
  yaml.each_pair do |k,v|
    insert_val = v
    if v.kind_of?(Array)
      insert_val = v.join ","
      mdbm_store(cluster, k, insert_val)
      v.each do |individual_value|
        puts "inserting clusters_norange: '[k, v, cluster]' = #{[k, individual_value, cluster].inspect}" if debug
        db.execute "insert into clusters_norange (key, value, cluster) values (?, ?, ?)", [k.encode("UTF-8"), individual_value.to_s.encode("UTF-8"), cluster.encode("UTF-8")]

      end
    else
      puts "inserting clusters_norange: '[k, v, cluster]' = #{[k, insert_val, cluster].inspect}" if debug
      db.execute "insert into clusters_norange (key, value, cluster) values (?, ?, ?)", [k.encode("UTF-8"), insert_val.to_s.encode("UTF-8"), cluster.encode("UTF-8")]
      mdbm_store(cluster, k, insert_val)
    end
    puts "inserting for clusters: '[k, v, cluster]' = #{[k, insert_val, cluster].inspect}" if debug
    db.execute "insert into clusters (key, value, cluster) values (?, ?, ?)", [k.encode("UTF-8"), insert_val.to_s.encode("UTF-8"), cluster.encode("UTF-8")]
  end
  forwardmap_time =  Time.now
  puts "   forward-map time: #{forwardmap_time - yamlload_time}" if debug
  keys = RangeLib.range_easy_expand($elr, "%#{cluster}:KEYS").read_array_of_string
  clusterkeys_time =  Time.now
  puts "   clusterkeys time: #{clusterkeys_time - forwardmap_time}" if debug
  keys.each do |key|
    values = RangeLib.range_easy_expand($elr, "%#{cluster}:#{key}").read_array_of_string
    values.each do |val|
      db.execute "insert into expanded_reverse_clusters (node, key, cluster) values (?, ?, ?)", [val.encode("UTF-8"), key.encode("UTF-8"), cluster.encode("UTF-8")]
    end
  end

  # calculate synthetic cluster slice :KEYS so backend functions can query it
  yaml.keys.each do |k|
    db.execute "insert into clusters_norange (key, value, cluster) values (?, ?, ?)", ["KEYS", k, cluster]
    db.execute "insert into clusters (key, value, cluster) values (?, ?, ?)", ["KEYS", k, cluster]
  end

  keys_range = yaml.keys.join ","
  mdbm_store(cluster, "KEYS", keys_range)

  allkeys_time =  Time.now
  puts "     allkeys time: #{allkeys_time - clusterkeys_time}" if debug
  puts "cluster #{cluster} took #{Time.now - percluster_time} to process" if debug
end

db.execute "insert into clusters (key, value, cluster) values (?, sqlite_version(), ?)", ["foo", "bar"]

db.execute "create index ix_clusters_cluster ON clusters(cluster)"
db.execute "create index ix_clusters_key_value ON clusters(key, value)"

db.execute "create index ix_expanded_reverse_clusters_node ON expanded_reverse_clusters(node)"
db.execute "create index ix_expanded_reverse_clusters_node_key ON expanded_reverse_clusters(node,key)"
db.execute "create index ix_expanded_reverse_clusters_cluster_node ON expanded_reverse_clusters(cluster,node)"

db.close

# $mdbm.close

File.rename sqlite_filename_tmp, sqlite_filename
File.rename mdbm_path_tmp, mdbm_path


