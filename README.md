libcrange
=========

A library for parsing and generating range expressions.

## Description

A library for parsing and generating range expressions. libcrange for modules to add symbolic name lookup features as well as functions to retreive additional data.

##Requirements

### Red Hat / Scientific / CentOS
* apr apr-devel flex pcre pcre-devel sqlite sqlite-devel libyaml libyaml-devel perl-YAML-Syck perl perl-core perl-devel perl-libs

### Ubuntu / Debian
* libapr1 libapr1-dev flex libpcre3 libpcre3-dev sqlite3 libsqlite3-dev libsqlite3-0 libyaml-0-2 libyaml-dev libyaml-syck-perl perl-base libperl5.14 libperl-dev

## Range Syntax

### Simple ranges:
  * node1,node2,node3,node4 == node1..node4 == node1..4
  * node1000..1099 == node1000..99 # auto pads digits to the end of the range
  * 1..100   # numeric only ranges
  * foo1-2.domain.com == foo1.domain.com-foo2.domain.com # domain support
  * 209.131.40.1-209.131.40.255 == 209.131.40.1-255 # IP ranges
      
      
### Clusters:
A cluster is a way to store node membership into groups. Depending on your backend module how this is stored may vary. In the case of the YAML module, a cluster is defined by the membership of the CLUSTER key
    
  * %cluster101 == nodes defined in /var/range/cluster101.yaml - Default key CLUSTER
  * %cluster101:ALL or %cluster101:FOO == nodes defined in a specific key of ks301/nodes.cf
  * %%all == assuming %all is a lust of clusters, the additional % will expand the list of clusters to node list that is their membership
  * *node == returns the cluster(s) that node is a member of
      
### Operatons:
  * range1,range2  == union
  * range1,-range2 == set difference
  * range1,&range2 == intersection
  * ^range1 == admins for the nodes in range1
  *  range1,-(range2,range3) == () can be used for grouping
  * range1,&/regex/ # all nodes in range1 that match regex
  * range1,-/regex/ # all nodes in range1 that do not match regex
      
### Advanced ranges:
    
   * foo{1,3,5} == foo1,foo3,foo5
   * %cluster30{1,3} == %cluster301,%cluster303
   * %cluster301-7 == nodes in clusters cluster301 to cluster307
   * %all:KEYS == all defined sections in cluster all
   * %{%all} == expands all clusters in %all
   * %all:dc1,-({f,k}s301-7) == names for clusters in dc1 except ks301-7,fs301-7
   * %all:dc1,-|ks| == clusters in dc1, except those matching ks
      
### Functions:

libcrange modules can define certain functions to look up data about hosts or clusters  The yaml module for instance implements some of the following functions.
    
  *  has(KEY;value) - looks for a cluster that has a key with some certain value
  *  allclusters() - returns a range of all clusters
  *  * and get_clusters(node)  - return the first cluster a node is a member of
  *  clusters(node) - return all clusters a node is a member of
  *  has(ENVIRONMENT; production) would return any clusters with the a key called ENVIRONMENT set to production
  * mem(CLUSTER; foo.example.com) => which keys under CLUSTER is foo.example.com a member of

Other functions can be added via modules that provide useful insight into your environment
    
  * vlan(host) - return vlan for this host
  * dc(host) - return datacenter for this host
  * drac(host) - might do an API call on backend to central host db to look up a drac IP for a host
