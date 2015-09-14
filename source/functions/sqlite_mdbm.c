#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sqlite3.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <sys/stat.h>
#include <ctype.h>

#include <mdbm.h>

#include "set.h"
#include "libcrange.h"
#include "range.h"
extern set * set_unpack(apr_pool_t* pool, void * packed_data);
char* _join_elements(apr_pool_t* pool, char sep, set* the_set);
static MDBM * mdbm_cache;


const char** functions_provided(libcrange* lr)
{
    static const char* functions[] = {"mem", "cluster", "clusters",
                                      "get_cluster", "get_groups", "group",
                                      "has", "allclusters", 0};
    return functions;
}

#define KEYVALUE_SQL "select key, value from clusters where cluster LIKE ?"
#define HAS_SQL "select cluster from clusters_norange where key LIKE ? and value LIKE ?"
#define GROUPS_SQL "select distinct key from expanded_reverse_clusters where node LIKE ? and cluster LIKE 'GROUPS'"
#define ALLCLUSTER_SQL "select distinct cluster from clusters"
#define CLUSTERS_SQL "select distinct cluster from expanded_reverse_clusters where node LIKE ? and key LIKE 'CLUSTER'"
#define MEM_SQL "select distinct key from expanded_reverse_clusters where cluster LIKE ? and node LIKE ?"
#define MMAP_PRAGMA_SQL "PRAGMA mmap_size=268435456"
#define EMPTY_STRING ""

#define DEFAULT_MDBM_DB "/tmp/range.mdbm"


sqlite3* _open_db(range_request* rr) 
{
    const char * sqlite_db_path;
    sqlite3* db;
    sqlite3_stmt* stmt;
    libcrange* lr = range_request_lr(rr);
    int err;
    
    /* open the db */
    if (!(db = libcrange_get_cache(lr, "sqlite:nodes"))) {
        sqlite_db_path = libcrange_getcfg(lr, "sqlitedb");
        if (!sqlite_db_path) sqlite_db_path = DEFAULT_SQLITE_DB;
        
        err = sqlite3_open(sqlite_db_path, &db);
        if (err != SQLITE_OK) {
          return NULL;
        }
        assert(err == SQLITE_OK);
        /* set mmap pragma */
        err = sqlite3_prepare(db, MMAP_PRAGMA_SQL, strlen(MMAP_PRAGMA_SQL), &stmt, NULL);
        if (err != SQLITE_OK) {
          range_request_warn(rr, "allclusters(): cannot query sqlite db");
          return NULL;
        }
        assert(err == SQLITE_OK);
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            // do nothing. Is this even necessary for the mmap_size pragma? docs are unclear
        }
        /* end mmap pragma setup */

        libcrange_set_cache(lr, "sqlite:nodes", db);
    }

    return db;
}

MDBM * _open_mdbm(range_request* rr)
{
  const char * mdbm_db_path;
  if (!mdbm_cache) {
    libcrange* lr = range_request_lr(rr);
    mdbm_db_path = libcrange_getcfg(lr, "mdbmdb");
    if (!mdbm_db_path) mdbm_db_path = DEFAULT_MDBM_DB;

    mdbm_cache = mdbm_open(mdbm_db_path, MDBM_O_RDONLY, 0, 0, 0);
    if (!mdbm_cache) { range_request_warn(rr, "cannot open mdbm"); }
    assert(mdbm_cache);
  }
  return mdbm_cache;
}

static char* _substitute_dollars(apr_pool_t* pool,
                                 const char* cluster, const char* line)
{
    static char buf[262144];
    char* dst = buf;
    int len = strlen(cluster);
    int in_regex = 0;
    char c;
    assert(line);
    assert(cluster);

    while ((c = *line) != '\0') {
        if (!in_regex && c == '$') {
            strcpy(dst, "cluster(");
            dst += sizeof("cluster(") - 1;
            strcpy(dst, cluster);
            dst += len;
            *dst++ = ':';
            c = *++line;
            while (isalnum(c) || c == '_') {
                *dst++ = c;
                c = *++line;
            }
            *dst++ = ')';
        }
        else if (c == '/') {
            in_regex = !in_regex;
            *dst++ = *line++;
        }
        else {
            *dst++ = *line++;
        }
    }
    *dst = '\0';
    return buf;
}



static set* _cluster_keys(range_request* rr, apr_pool_t* pool,
                          const char* cluster)
{
    set* sections;
    sqlite3* db;
    sqlite3_stmt* stmt;
    int err;
    db = _open_db(rr);
    
    /* our return set */
    sections = set_new(pool, 0);

    
    /* prepare our select */
    err = sqlite3_prepare(db, KEYVALUE_SQL, strlen(KEYVALUE_SQL),
                          &stmt, NULL);
    if (err != SQLITE_OK) {
      range_request_warn(rr, "%s: cannot query sqlite db", cluster);
      return sections;
    }
    assert(err == SQLITE_OK);

    /* for each key/value pair in cluster */
    sqlite3_bind_text(stmt, 1, cluster, strlen(cluster), SQLITE_STATIC);
    while(sqlite3_step(stmt) == SQLITE_ROW) {
        /* add it to the return */
        const char* key = (const char*)sqlite3_column_text(stmt, 0);
        const char* value = (const char*)sqlite3_column_text(stmt, 1);
        if (NULL == value) {
            value = EMPTY_STRING;
        }
        set_add(sections, key, apr_psprintf(pool, "%s",  _substitute_dollars(pool, cluster, value) ));
    }

    /* Add the magic "KEYS" index */
    set_add(sections, "KEYS", _join_elements(pool, ',', sections));
    
    sqlite3_finalize(stmt);
    return sections;
}

typedef struct cache_entry
{
    time_t mtime;
    apr_pool_t* pool;
    set* sections;
} cache_entry;

char* _join_elements(apr_pool_t* pool, char sep, set* the_set)
{
    set_element** members = set_members(the_set);
    set_element** p = members;
    int total;
    char* result;
    char* p_res;

    total = 0;
    while (*p) {
        total += strlen((*p)->name) + 1;
        ++p;
    }
    if (!total) return NULL;

    p = members;
    p_res = result = apr_palloc(pool, total);
    while (*p) {
        int len = strlen((*p)->name);
        strcpy(p_res, (*p)->name);
        ++p;
        p_res += len;
        *p_res++ = sep;
    }

    *--p_res = '\0';
    return result;
}

#define MAX_CLUSTER_STRING 8192
static char fetch_key[MAX_CLUSTER_STRING];

static range* _expand_cluster(range_request* rr, const char* cluster, const char* section)
{
  set * ret_set;
  MDBM * db = _open_mdbm(rr);
  apr_pool_t* req_pool = range_request_pool(rr);
  // return a range * of the section
  // build the key:val
  strncpy(fetch_key, cluster, MAX_CLUSTER_STRING);
  strncat(fetch_key, ":", MAX_CLUSTER_STRING);
  strncat(fetch_key, section, MAX_CLUSTER_STRING);
  // first, query the data from mdbm
  datum val;
  datum key;
  key.dptr = fetch_key;
  key.dsize = strlen(fetch_key);
  val = mdbm_fetch(db, key);
  if (val.dsize) {
    ret_set = set_unpack(req_pool, val.dptr);
    return range_from_set(rr, ret_set);
  } else {
    /* FIXME warn -- ok fixed*/
    range_request_warn_type(rr, "NOCLUSTER", fetch_key);
    return range_new(rr);
  }
}

/* get a list of all clusters */
static const char** _all_clusters(range_request* rr)
{
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int err, n, i;
    apr_pool_t* pool = range_request_pool(rr);
    set* clusters = set_new(pool, 0);
    set_element** elts;
    const char** table;
    
    db = _open_db(rr);
    err = sqlite3_prepare(db, ALLCLUSTER_SQL, strlen(ALLCLUSTER_SQL),
                          &stmt, NULL);
    if (err != SQLITE_OK) {
      range_request_warn(rr, "allclusters(): cannot query sqlite db");
      return NULL;
    }
    assert(err == SQLITE_OK);

    /* for each cluster */
    while(sqlite3_step(stmt) == SQLITE_ROW) {
        const char* cluster = (const char*)sqlite3_column_text(stmt, 0);
        set_add(clusters, cluster, 0);
    }
    
    n = clusters->members;
    table = apr_palloc(pool, sizeof(char*) * (n + 1));
    table[n] = NULL;

    elts = set_members(clusters);
    for (i=0; i<n; i++) {
        const char* name = (*elts++)->name;
        table[i] = name;
    }

    sqlite3_finalize(stmt);
    return table;
}

range* rangefunc_allclusters(range_request* rr, range** r)
{
    range* ret = range_new(rr);

    const char** all_clusters = _all_clusters(rr);
    const char** cluster = all_clusters;
    int warn_enabled = range_request_warn_enabled(rr);

    if (!cluster) return ret;

    range_request_disable_warns(rr);
    while (*cluster) {
        range_add(ret, *cluster);
        cluster++;
    }
    if (warn_enabled) range_request_enable_warns(rr);

    return ret;
}


range* _do_has_mem(range_request* rr, range** r, char* sql_query)
{
    sqlite3* db;
    sqlite3_stmt* stmt;
    int err;
    range* ret = range_new(rr);
    apr_pool_t* pool = range_request_pool(rr);
    if (NULL == r[0]) {
        // don't attempt anything without arg #1 (key)
        return ret;
    }
    const char** tag_names = range_get_hostnames(pool, r[0]);
    const char* tag_name = tag_names[0];
    if (NULL == tag_name) {
        return ret;
    }

    const char** tag_values;
    const char* tag_value;
    if (NULL == r[1]) {
        // if we don't have arg #2 (val) then search for keys with empty string value
        tag_value = EMPTY_STRING;
    } else {
        tag_values = range_get_hostnames(pool, r[1]);
        tag_value = tag_values[0];
    }

    db = _open_db(rr);
    err = sqlite3_prepare(db, sql_query, strlen(sql_query), &stmt,
                          NULL);
    if (err != SQLITE_OK) {
      range_request_warn(rr, "has(%s,%s): cannot query sqlite db", tag_name, tag_value);
      return ret;
    }
    assert(err == SQLITE_OK);

    sqlite3_bind_text(stmt, 1, tag_name, strlen(tag_name), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, tag_value, strlen(tag_value), SQLITE_STATIC);

    while(sqlite3_step(stmt) == SQLITE_ROW) {
        const char* answer = (const char*)sqlite3_column_text(stmt, 0);
        range_add(ret, answer);
    }

    sqlite3_finalize(stmt);
    return ret;
}

range* rangefunc_has(range_request* rr, range** r) {
    return _do_has_mem(rr, r, HAS_SQL);
}

range* rangefunc_mem(range_request* rr, range** r)
{
    return _do_has_mem(rr, r, MEM_SQL);
}

static set* _get_clusters(range_request* rr)
{
    const char** all_clusters = _all_clusters(rr);
    const char** p_cl = all_clusters;
    apr_pool_t* pool = range_request_pool(rr);
    set* node_cluster = set_new(pool, 40000);

    if(p_cl == NULL) {
        return node_cluster;
    }

    while (*p_cl) {
        range* nodes_r = _expand_cluster(rr, *p_cl, "CLUSTER");
        const char** nodes = range_get_hostnames(pool, nodes_r);
        const char** p_nodes = nodes;

        while (*p_nodes) {
            apr_array_header_t* clusters = set_get_data(node_cluster, *p_nodes);

            if (!clusters) {
                clusters = apr_array_make(pool, 1, sizeof(char*));
                set_add(node_cluster, *p_nodes, clusters);
            }

            *(const char**)apr_array_push(clusters) = *p_cl;
            ++p_nodes;
        }
        ++p_cl;
    }

    return node_cluster;
}


range* rangefunc_get_cluster(range_request* rr, range** r)
{
    sqlite3* db;
    sqlite3_stmt* stmt;
    int err;
    
    range* ret = range_new(rr);
    apr_pool_t* pool = range_request_pool(rr);
    const char** nodes = range_get_hostnames(pool, r[0]);
    const char** p_nodes = nodes;
    
    db = _open_db(rr);
    err = sqlite3_prepare(db, CLUSTERS_SQL, strlen(CLUSTERS_SQL), &stmt, NULL);
    if (err != SQLITE_OK) {
        range_request_warn(rr, "clusters(): cannot query sqlite db");
        return ret;
    }
    
    while (*p_nodes) {
        const char * node_name = *p_nodes;
        sqlite3_bind_text(stmt, 1, node_name, strlen(node_name), SQLITE_STATIC);
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            const char* answer = (const char*)sqlite3_column_text(stmt, 0);
            range_add(ret, answer);
        }
        break; // get_cluster() returns zero or one result only
    }
    sqlite3_finalize(stmt);
    return ret;
}

range* rangefunc_clusters(range_request* rr, range** r)
{
    sqlite3* db;
    sqlite3_stmt* stmt;
    int err;

    range* ret = range_new(rr);
    apr_pool_t* pool = range_request_pool(rr);
    const char** nodes = range_get_hostnames(pool, r[0]);
    const char** p_nodes = nodes;

    db = _open_db(rr);
    err = sqlite3_prepare(db, CLUSTERS_SQL, strlen(CLUSTERS_SQL), &stmt, NULL);
    if (err != SQLITE_OK) {
        range_request_warn(rr, "clusters(): cannot query sqlite db");
        return ret;
    }

    while (*p_nodes) {
        const char * node_name = *p_nodes;
        sqlite3_bind_text(stmt, 1, node_name, strlen(node_name), SQLITE_STATIC);
        while(sqlite3_step(stmt) == SQLITE_ROW) {
            const char* answer = (const char*)sqlite3_column_text(stmt, 0);
            range_add(ret, answer);
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        ++p_nodes;
    }

    sqlite3_finalize(stmt);
    return ret;
}

range* rangefunc_cluster(range_request* rr, range** r)
{
    range* ret = range_new(rr);
    apr_pool_t* pool = range_request_pool(rr);
    const char** clusters = range_get_hostnames(pool, r[0]);
    const char** p = clusters;

    while (*p) {
        const char* colon = strchr(*p, ':');
        range* r1;
        if (colon) {
            int len = strlen(*p);
            int cluster_len = colon - *p;
            int section_len = len - cluster_len - 1;

            char* cl = apr_palloc(pool, cluster_len + 1);
            char* sec = apr_palloc(pool, section_len + 1);

            memcpy(cl, *p, cluster_len);
            cl[cluster_len] = '\0';
            memcpy(sec, colon + 1, section_len);
            sec[section_len] = '\0';

            r1 = _expand_cluster(rr, cl, sec);
        }
        else
            r1 = _expand_cluster(rr, *p, "CLUSTER");

        if (range_members(r1) > range_members(ret)) {
            /* swap them */
            range* tmp = r1;
            r1 = ret;
            ret = tmp;
        }
        range_union_inplace(rr, ret, r1);
        range_destroy(r1);
        ++p;
    }
    return ret;

}

range * rangefunc_group(range_request* rr, range** r)
{
    sqlite3* db;
    db = _open_db(rr);
    range* ret = range_new(rr), *expanded;
    apr_pool_t* pool = range_request_pool(rr);
    const char** groups = range_get_hostnames(pool, r[0]);
    while (*groups) {
        expanded = _expand_cluster(rr, "GROUPS", *groups);
        range_union_inplace(rr, ret, expanded);
        ++groups;
    }
    return ret;
}

range* rangefunc_get_groups(range_request* rr, range** r)
{
    sqlite3* db;
    sqlite3_stmt* stmt;
    int err;
    range* ret = range_new(rr);
    apr_pool_t* pool = range_request_pool(rr);
    const char** tag_names = range_get_hostnames(pool, r[0]);

    const char* tag_name = tag_names[0];

    if (NULL == tag_name) {
        return ret;
    }

    db = _open_db(rr);
    err = sqlite3_prepare(db, GROUPS_SQL, strlen(GROUPS_SQL), &stmt, NULL);
    if (err != SQLITE_OK) {
        range_request_warn(rr, "?%s: cannot query sqlite db", tag_name );
        return ret;
    }
    assert(err == SQLITE_OK);

    sqlite3_bind_text(stmt, 1, tag_name, strlen(tag_name), SQLITE_STATIC);

    while(sqlite3_step(stmt) == SQLITE_ROW) {
        const char* answer = (const char*)sqlite3_column_text(stmt, 0);
        range_add(ret, answer);
    }
    sqlite3_finalize(stmt);
    return ret;
}



