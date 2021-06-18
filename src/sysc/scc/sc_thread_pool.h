/*******************************************************************************
 * Copyright 2021 MINRES Technologies GmbH
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *******************************************************************************/

#ifndef OSCI_LIB_SCC_SRC_SYSC_SCC_SC_THREAD_POOL_H_
#define OSCI_LIB_SCC_SRC_SYSC_SCC_SC_THREAD_POOL_H_
#include <systemc>
#include <cci_configuration>
#include <functional>

namespace scc {

class sc_thread_pool:sc_core::sc_object {
public:
    sc_thread_pool();
    virtual ~sc_thread_pool();

    void execute(std::function<void(void)> fct);

    cci::cci_param<unsigned> max_concurrent_threads{"max_concurrent_threads", 16};

private:
    sc_core::sc_fifo<std::function<void(void)>> dispatch_queue{"dispatch_queue"};
    unsigned thread_avail{0}, thread_active{0};

};
} /* namespace scc */

#endif /* OSCI_LIB_SCC_SRC_SYSC_SCC_SC_THREAD_POOL_H_ */
