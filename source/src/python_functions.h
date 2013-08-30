#ifndef _PYTHON_FUNCTIONS_H
#define _PYTHON_FUNCTIONS_H

#include "libcrange.h"
#include "set.h"
#include "range.h"

int add_functions_from_pythonmodule(libcrange* lr, apr_pool_t* pool,
                                    set* pythonfunctions,
                                    const char* module, const char* prefix);

range * python_function(range_request* rr,
                       const char* funcname, const range** r, range ** result);

#endif
