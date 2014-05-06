#!/usr/bin/ruby
# transform yamlfile range datadir into sqlite
# usage $0 --rangedata /path/to/yamfiles --module_path /usr/lib/libcrange 

require 'rubygems'

require 'sqlite3'
require 'rangelib'
require 'yaml'
require 'getoptlong'
require 'tempfile'



opts = GetoptLong.new(['--rangedata', GetoptLong::REQUIRED_ARGUMENT],
                      ['--module_path', GetoptLong::REQUIRED_ARGUMENT],
                      ['--help', '-h', GetoptLong::NO_ARGUMENT])

rangedata_path = "" # range data dir
module_path = ""    # libcrange plugin dir
opts.each do |opt, arg|
  case opt
  when '--rangedata'
    rangedata_path = arg
  when '--module_path'
    module_path = arg
  end
end

sqlite_filename = File.join rangedata_path, "cluster.sqlite"

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

File.unlink sqlite_filename rescue true
db = SQLite3::Database.new sqlite_filename

db.execute "PRAGMA journal_mode = OFF"
# the forward map, using range expr
db.execute "create table clusters (key varchar, val varchar, cluster varchar);"

# node to containing clusters. we only use key=CLUSTER
db.execute "create table expanded_reverse_clusters (node varchar, key varchar, cluster varchar);" 


all_clusters.each do |cluster| 
  percluster_time = Time.now
  yaml = YAML.load_file("#{rangedata_path}/#{cluster}.yaml")
  yamlload_time = Time.now
  puts "   yaml load time: #{yamlload_time - percluster_time}"
  yaml.each_pair do |k,v|
    db.execute "insert into clusters (key, val, cluster) values (?, ?, ?)", [k, v, cluster]
  end
  forwardmap_time =  Time.now
  puts "   forward-map time: #{forwardmap_time - yamlload_time}"
  keys = RangeLib.range_easy_expand(elr, "%#{cluster}:KEYS").read_array_of_string
  clusterkeys_time =  Time.now
  puts "   clusterkeys time: #{clusterkeys_time - forwardmap_time}"
  keys.each do |key|
    values = RangeLib.range_easy_expand(elr, "%#{cluster}:#{key}").read_array_of_string
    values.each do |val|
      db.execute "insert into expanded_reverse_clusters (node, key, cluster) values (?, ?, ?)", [val, key, cluster]
    end
  end
  allkeys_time =  Time.now
  puts "     allkeys time: #{allkeys_time - clusterkeys_time}"
  puts "cluster #{cluster} took #{Time.now - percluster_time} to process"
end


db.close
