#include "python_functions.h"
#include <apr_strings.h>
#include <Python.h>

#define _QUOTEME(x) #x
#define QUOTEME(x) _QUOTEME(x)


#ifndef PYTHON_MODULE_PATH
 #define PYTHON_MODULE_PATH /var/libcrange/python
#endif


#define PYTHON_BOOT \
  "import sys\n" \
  "sys.path.append('" QUOTEME(PYTHON_MODULE_PATH) "')\n"    \
  "libcrange_funcs = {}\n" \
  "def libcrange_load_file(module):\n" \
  "    mod = globals()[module]\n"\
  "    for funcname in mod.functions_provided():\n"\
  "        libcrange_funcs[funcname] = getattr(mod, funcname)\n"\
  "    return mod.functions_provided()\n"\
  "\n"\
  "\n"\
  "def libcrange_call_func(funcname, *range):\n"\
  "    return libcrange_funcs[funcname](*range)\n"\
  "\n"\
  "def libcrange_load_path(path):\n"\
  "    sys.path += path.split(':')\n"\
  "\n"\
  "\n"\
  ""

static const char**
get_and_load_exported_functions(libcrange* lr, apr_pool_t* pool,
                       const char * module, const char* prefix)
{
  const char** functions = NULL;
  PyObject *pLibcrangeLoadFile;
  PyObject *pFunctionsProvided;
  PyObject *pModuleName;

  PyObject * pMain = PyImport_AddModule("__main__"); // borrowed ref
  pLibcrangeLoadFile = PyObject_GetAttrString(pMain, "libcrange_load_file");
  if (pLibcrangeLoadFile && PyCallable_Check(pLibcrangeLoadFile)) {
    pModuleName = PyString_FromString(module);
    pFunctionsProvided = PyObject_CallFunctionObjArgs(pLibcrangeLoadFile, pModuleName, NULL);
    int count = PyList_Size(pFunctionsProvided);
    int i=0;
    functions = apr_palloc(pool, sizeof(char*) * (count + 1));
    for (i = 0; i < count; i++) {
      PyObject *item = PyList_GetItem(pFunctionsProvided, i);
      functions[i] = apr_pstrdup(pool, PyString_AsString(item));

      Py_DECREF(item);
    }
    Py_DECREF(pModuleName);
    Py_DECREF(pLibcrangeLoadFile);
    Py_DECREF(pFunctionsProvided);

  }
  return functions;
}

static void destruct_python(void)
{
  // cleanup python interp FIXME
}

int add_functions_from_pythonmodule(libcrange* lr, apr_pool_t* pool,
                                  set* pythonfunctions,
                                  const char* module, const char* prefix)
{
    const char** exported_functions;
    const char** p;
    const char* module_copy = apr_pstrdup(pool, module);
    const char *python_inc_path = 0;
    static int python_interp = 0;
    PyObject *pModuleName;
    PyObject *pModule;

    // bootstrap the python interpreter if it hasn't been done yet
    if (!python_interp) {
      Py_Initialize();
      // Set up some basic stuff to run the plugins
      PyRun_SimpleString(PYTHON_BOOT);

      python_inc_path = libcrange_getcfg(lr, "python_inc_path");
      if (python_inc_path) {
        // FIXME this doesn't work properly
        // PyImport_ImportModule(module) returns NULL unless a .pyc has been generated
        // after the .pyc is generated things are OK, but only if PYTHONPATH is also set
        PyObject * pMain = PyImport_AddModule("__main__");
        PyObject * pPyIncPath;
        PyObject * pLibcrangeLoadFile;
        pLibcrangeLoadFile = PyObject_GetAttrString(pMain, "libcrange_load_file");
        pPyIncPath = PyString_FromString(python_inc_path);
        PyObject_CallFunctionObjArgs(pLibcrangeLoadFile, pMain, pPyIncPath, NULL); // return None

        Py_DECREF(pLibcrangeLoadFile);
        Py_DECREF(pPyIncPath);
      }
    }

    // import this particular module
    pModule = PyImport_ImportModule(module);
    if (pModule == NULL) {
      printf("ERR: pModule == NULL, module: %s, prefix: %s\n", module, prefix);
      return 0;
    }

    {
      // insert pModule into globals()[module]
      PyObject * pMain = PyImport_AddModule("__main__");
      PyObject_SetAttrString(pMain, module, pModule);
      // pMain borrowed reference, no decref
    }

    /* get the list of functions exported by this module */
    p = exported_functions = get_and_load_exported_functions(lr, pool,
                                                    module, prefix);

    while (*p) {
      /* add functions to the set seen by libcrange */
      set_add(pythonfunctions, *p, (void*)module_copy);
      ++p;
    }

    return 0;
}

PyObject * range_to_py_array(apr_pool_t* pool, const range* r)
{
  int i,node_count;
  const char** nodes;
  PyObject * py_list = PyList_New(0);
  assert(r);
  node_count = r->nodes->members;
  nodes = range_get_hostnames(pool, r);
  for (i=0; i < node_count; i++) {
    PyObject *item;
    item = PyString_FromString(nodes[i]);
    PyList_Append(py_list, item);
    Py_DECREF(item);
  }
  return py_list;
}

static range* _python_function(range_request* rr,
                     const char* funcname, const range** r)
{
  range* result = range_new(rr);
  PyObject * pLibcrangeCallFunc;
  PyObject * pNodesReturned;
  PyObject * pMain = PyImport_AddModule("__main__");
  // printf("rr: %p, funcname=%s, range**r = %p\n", rr, funcname, r);
  pLibcrangeCallFunc = PyObject_GetAttrString(pMain, "libcrange_call_func");
  
  if (pLibcrangeCallFunc && PyCallable_Check(pLibcrangeCallFunc)) {
    PyObject * pRangeFuncName;
    PyObject * item;
    pRangeFuncName = PyString_FromString(funcname);
    PyObject * pFuncArgs;
    PyObject * pTempArgList = PyList_New(0);
    PyList_Append(pTempArgList, pRangeFuncName);
    Py_DECREF(pRangeFuncName);
    
    // build our range** into python function args
    const range** p_r = r;
    while (*p_r) {
      item = range_to_py_array(range_request_pool(rr), *p_r);
      PyList_Append(pTempArgList, item);
      Py_DECREF(item);
      p_r++;
    }
    pFuncArgs = PyList_AsTuple(pTempArgList);
    Py_DECREF(pTempArgList);
    
    // call the function
    pNodesReturned = PyObject_CallObject(pLibcrangeCallFunc, pFuncArgs);
    Py_DECREF(pFuncArgs);
    PyObject *iterator = PyObject_GetIter(pNodesReturned);
    if (iterator == NULL) {
      printf("ERROR: python function %s ran, but didn't return an iteratable object", funcname);
      return result;
    }
    // an iteratable object was returned, transform it into result
    while (item = PyIter_Next(iterator)) {
      // PyObject_Print(item, stdout, 0 );
      range_add(result, PyString_AsString(item));
      Py_DECREF(item);
    }
    Py_DECREF(pNodesReturned);
  }

  return result;
}

range * python_function(range_request* rr,
                       const char* funcname, const range** r, range ** result)
{
  /* dispatch to _python_function */ 
    *result = _python_function(rr, funcname, r);
    return *result;
}
