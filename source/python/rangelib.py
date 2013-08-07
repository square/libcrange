#!/usr/bin/python

from cffi import FFI

class RangeLib(object):
    def __init__(self, config_file):
        self.ffi = FFI()
        self.ffi.cdef("""
    typedef void easy_lr ; // avoid exposing the struct internals, fake it as void
    easy_lr* range_easy_create(const char* config_file);
    const char ** range_easy_expand(easy_lr* elr, const char * c_range);
    const char * range_easy_eval(easy_lr* elr, const char * c_range);
    char * range_easy_compress(easy_lr* elr, const char ** c_nodes);
    int range_easy_destroy(easy_lr* elr);

    void free(void *ptr);
""")
        self.rangelib_ffi = self.ffi.dlopen("libcrange.so")
        self.libc_ffi = self.ffi.dlopen("libc.so.6")
        self.elr = self.rangelib_ffi.range_easy_create(self.ffi.new("char[]", config_file))

    def __charpp_to_native(self, arg):
        i = 0
        arr = []
        while arg[i] != self.ffi.NULL:
            x = self.ffi.string(arg[i])
            self.libc_ffi.free(arg[i])
            arr.append(x) 
            i += 1
        self.libc_ffi.free(arg)
        return arr

    def expand(self, c_range):
        ret = self.rangelib_ffi.range_easy_expand(self.elr, self.ffi.new("char[]", c_range))
        x = self.__charpp_to_native(ret)
        return x

    def compress(self, nodes):
        char_arg = [ self.ffi.new("char[]", x) for x in nodes ]
        char_arg.append(self.ffi.NULL)
        retptr = self.rangelib_ffi.range_easy_compress(self.elr, self.ffi.new("char*[]", char_arg))
        ret = self.ffi.string(retptr)
        self.libc_ffi.free(retptr)
        return ret

    def eval(self, c_range):
        retptr = self.rangelib_ffi.range_easy_eval(self.elr, self.ffi.new("char[]", c_range))
        ret = self.ffi.string(retptr)
        self.libc_ffi.free(retptr)
        return ret

    def __del__(self):
        self.rangelib_ffi.range_easy_destroy(self.elr)


if __name__ == "__main__":
    # test it
    x = RangeLib("/etc/range.conf")
    print "EXPAND: " + str(x.expand("foo"))
    print "COMPRESS: " + x.compress(["foo01", "foo02"])
    print "EVAL: " + x.eval("foo01,foo02")
