/**
 * Copyright (C) 2016-2020 Xilinx, Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include "lop.h"
#include "core/common/module_loader.h"
#include "core/common/utils.h"
#include "core/common/dlfcn.h"

namespace xdp {
namespace lop {

  // The loading of the function should only happen once.  Since it 
  //  could theoretically be called from two user threads at once, we
  //  use an internal struct constructor that is thread safe to ensure
  //  it only happens once
  void load()
  {
    // Thread safe per C++-11
    static xrt_core::module_loader xdp_lop_loader("xdp_lop_plugin",
						  register_functions,
						  warning_function,
                                                  error_function) ;
  }

  // All of the function pointers that will be dynamically linked from
  //  the XDP Plugin side
  std::function<void (const char*, long long int, unsigned long long int)> function_start_cb;
  std::function<void (const char*, long long int, unsigned long long int)> function_end_cb;
  std::function<void (unsigned int, bool)> read_cb ;
  std::function<void (unsigned int, bool)> write_cb ;
  std::function<void (unsigned int, bool)> enqueue_cb ;

  void register_functions(void* handle)
  {
    typedef void (*ftype)(const char*, long long int, unsigned long long int) ;
    function_start_cb = (ftype)(xrt_core::dlsym(handle, "lop_function_start")) ;
    if (xrt_core::dlerror() != NULL) function_start_cb = nullptr ;    

    function_end_cb = (ftype)(xrt_core::dlsym(handle, "lop_function_end"));
    if (xrt_core::dlerror() != NULL) function_end_cb = nullptr ;

    typedef void (*btype)(unsigned int, bool) ;

    read_cb = (btype)(xrt_core::dlsym(handle, "lop_read")) ;
    if (xrt_core::dlerror() != NULL) read_cb = nullptr ;
    
    write_cb = (btype)(xrt_core::dlsym(handle, "lop_write")) ;
    if (xrt_core::dlerror() != NULL) write_cb = nullptr ;

    enqueue_cb = (btype)(xrt_core::dlsym(handle, "lop_kernel_enqueue")) ;
    if (xrt_core::dlerror() != NULL) enqueue_cb = nullptr ;
  }

  void warning_function()
  {
    if (xrt_xocl::config::get_profile() || xrt_xocl::config::get_opencl_summary())
    {
      xrt_xocl::message::send(xrt_xocl::message::severity_level::warning,
			 "Both low overhead profiling and OpenCL profile summary generation are enabled.  The trace generated by low overhead profiling will reflect the higher overhead associated with profile summary generation.  For best performance of low overhead profiling, please disable standard OpenCL profiling.\n") ;
    }
  }

  int error_function()
  {
    if (xrt_xocl::config::get_opencl_trace() || xrt_xocl::config::get_timeline_trace())
    {
      xrt_xocl::message::send(xrt_xocl::message::severity_level::warning,
			 "Both low overhead profiling and OpenCL trace are enabled. Disabling LOP trace as it cannot be used together with OpenCL trace.\n") ;
      return 1;
    }
    return 0;
  }

  FunctionCallLogger::FunctionCallLogger(const char* function) :
    FunctionCallLogger(function, 0)
  {    
  }

  FunctionCallLogger::FunctionCallLogger(const char* function,
                                         long long int address) :
    m_funcid(0), m_name(function), m_address(address)
  {
    // The LOP plugin should have been loaded since the OpenCL hooks
    //  are all before the LOP hooks

    // Log the trace for this function
    if (function_start_cb) {
      m_funcid = xrt_core::utils::issue_id() ;
      function_start_cb(m_name, m_address, m_funcid) ;
    }
  }

  FunctionCallLogger::~FunctionCallLogger()
  {
    if (function_end_cb)
      function_end_cb(m_name, m_address, m_funcid) ;
  }

} // end namespace lop
} // end namespace xdp

namespace xocl {
  namespace lop {

    // Create lambda functions that will be attached and triggered
    //  by events when their status changes
    std::function<void (xocl::event*, cl_int)> 
    action_read()
    {
      return [](xocl::event* e, cl_int status) 
	{
	  if (!xdp::lop::read_cb) return ;

	  // Only keep track of the start and stop
	  if (status == CL_RUNNING)
	    xdp::lop::read_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE) 
	    xdp::lop::read_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)> 
    action_write()
    {
      return [](xocl::event* e, cl_int status)
	{
	  if (!xdp::lop::write_cb) return ;

	  // Only keep track of the start and stop
	  if (status == CL_RUNNING)
	    xdp::lop::write_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE) 
	    xdp::lop::write_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)> 
    action_migrate(cl_mem_migration_flags flags) 
    {
      if (flags & CL_MIGRATE_MEM_OBJECT_HOST)
      {
	return [](xocl::event* e, cl_int status)
	  {
	    if (!xdp::lop::read_cb) return ;

	    if (status == CL_RUNNING)
	      xdp::lop::read_cb(e->get_uid(), true) ;
	    else if (status == CL_COMPLETE)
	      xdp::lop::read_cb(e->get_uid(), false) ;
	  } ;
      }
      else
      {
	return [](xocl::event* e, cl_int status)
	  {
	    if (!xdp::lop::write_cb) return ;

	    if (status == CL_RUNNING)
	      xdp::lop::write_cb(e->get_uid(), true) ;
	    else if (status == CL_COMPLETE)
	      xdp::lop::write_cb(e->get_uid(), false) ;
	  } ;
      }
    }

    std::function<void (xocl::event*, cl_int)> 
    action_ndrange()
    {
      return [](xocl::event* e, cl_int status)
	{
	  if (!xdp::lop::enqueue_cb) return ;

	  if (status == CL_RUNNING)
	    xdp::lop::enqueue_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE)
	    xdp::lop::enqueue_cb(e->get_uid(), false) ;
	} ;
    }

    std::function<void (xocl::event*, cl_int)>
    action_ndrange_migrate(cl_kernel kernel)
    {
      // Only check to see if any of the memory objects are going
      //  to move.
      bool writeWillHappen = false ;
      for (auto& arg : xocl::xocl(kernel)->get_xargument_range())
      {
	auto mem = arg->get_memory_object() ;
	if (mem != nullptr && !(mem->is_resident()))
	{
	  writeWillHappen = true ;
	  break ;
	}
      }
      
      if (writeWillHappen)
      {
	return [](xocl::event* e, cl_int status)
	{
	  if (!xdp::lop::write_cb) return ;
	  
	  if (status == CL_RUNNING)
	    xdp::lop::write_cb(e->get_uid(), true) ;
	  else if (status == CL_COMPLETE)
	    xdp::lop::write_cb(e->get_uid(), false) ;
	} ;	
      }
      else
      {
	return [](xocl::event* e, cl_int status)
	  {
	    return ;
	  } ;
      }
    }

  } // end namespace lop
} // end namespace xocl
