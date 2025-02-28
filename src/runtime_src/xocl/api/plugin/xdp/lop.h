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

#ifndef LOP_DOT_H
#define LOP_DOT_H

/**
 * This file contains the callback mechanisms for connecting the OpenCL
 * layer to the low overhead profiling XDP plugin.
 */

#include <functional>
#include <atomic>

#include "core/common/config_reader.h"
#include "xocl/core/event.h"

// This namespace contains the functions responsible for loading and
//  linking the LOP functions.
namespace xdp {
namespace lop {

  // The top level function that loads the library.  This should
  //  only be executed once
  void load() ;

  // The function that makes connections via dynamic linking and dynamic symbols
  void register_functions(void* handle) ;

  // A function that outputs any warnings based upon status and configuration
  void warning_function() ;

  // Check and warn if opencl/timeline trace are enabled
  int error_function();
  
  // Every OpenCL API we are interested in will have an instance
  //  of this class constructed at the start
  class FunctionCallLogger
  {
  private:
    uint64_t m_funcid ;
    const char* m_name = nullptr ;
    long long int m_address = 0 ;
  public:
    FunctionCallLogger(const char* function) ;
    FunctionCallLogger(const char* function, long long int address) ;
    ~FunctionCallLogger() ;
  } ;

} // end namespace lop
} // end namespace xdp

namespace xocl {
  namespace lop {

    template <typename F, typename ...Args>
    inline void
    set_event_action(xocl::event* event, F&& f, Args&&... args)
    {
      if (xrt_core::config::get_lop_trace())
	event->set_lop_action(f(std::forward<Args>(args)...));
    }

    std::function<void (xocl::event*, cl_int)> action_read() ;
    std::function<void (xocl::event*, cl_int)> action_write() ;
    std::function<void (xocl::event*, cl_int)> action_migrate(cl_mem_migration_flags flags) ;
    std::function<void (xocl::event*, cl_int)> action_ndrange() ;
    std::function<void (xocl::event*, cl_int)> action_ndrange_migrate(cl_kernel kernel) ;
    //std::function<void (xocl::event*)> action_map() ;
    //std::function<void (xocl::event*)> action_unmap() ;
    //std::function<void (xocl::event*)> action_copy() ;
    
  } // end namespace lop
} // end namespace xdp

// Helpful defines
#define LOP_LOG_FUNCTION_CALL xdp::lop::FunctionCallLogger LOPObject(__func__);
#define LOP_LOG_FUNCTION_CALL_WITH_QUEUE(Q) xdp::lop::FunctionCallLogger LOPObject(__func__, (long long int)Q);

#endif
