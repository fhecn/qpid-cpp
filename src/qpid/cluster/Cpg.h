#ifndef CPG_H
#define CPG_H

/*
 *
 * Copyright (c) 2006 The Apache Software Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "qpid/Exception.h"
#include "qpid/cluster/Dispatchable.h"

#include <cassert>
#include <string.h>

extern "C" {
#include <openais/cpg.h>
}

namespace qpid {
namespace cluster {

/**
 * Lightweight C++ interface to cpg.h operations. 
 * Manages a single CPG handle, initialized in ctor, finialzed in destructor.
 * On error all functions throw Cpg::Exception
 */
class Cpg : public Dispatchable {
  public:
    struct Exception : public ::qpid::Exception {
        Exception(const std::string& msg) : ::qpid::Exception(msg) {}
    };

    struct Name : public cpg_name {
        Name(const char* s) { copy(s, strlen(s)); }
        Name(const char* s, size_t n) { copy(s,n); }
        Name(const std::string& s) { copy(s.data(), s.size()); }
        void copy(const char* s, size_t n) {
            assert(n < CPG_MAX_NAME_LENGTH);
            memcpy(value, s, n);
            length=n;
        }

        std::string str() const { return std::string(value, length); }
    };
    
    struct Id {
        uint64_t id;
        Id(uint64_t n=0) : id(n) {}
        Id(uint32_t nodeid, uint32_t pid) { id=(uint64_t(nodeid)<<32)+ pid; }
        Id(const cpg_address& addr) : id(Id(addr.nodeid, addr.pid)) {}

        operator uint64_t() const { return id; }
        uint32_t nodeId() const { return id >> 32; }
        pid_t pid() const { return id & 0xFFFF; }
    };

    static std::string str(const cpg_name& n) {
        return std::string(n.value, n.length);
    }

    struct Handler {
        virtual ~Handler() {};
        virtual void deliver(
            cpg_handle_t /*handle*/,
            struct cpg_name *group,
            uint32_t /*nodeid*/,
            uint32_t /*pid*/,
            void* /*msg*/,
            int /*msg_len*/) = 0;

        virtual void configChange(
            cpg_handle_t /*handle*/,
            struct cpg_name */*group*/,
            struct cpg_address */*members*/, int /*nMembers*/,
            struct cpg_address */*left*/, int /*nLeft*/,
            struct cpg_address */*joined*/, int /*nJoined*/
        ) = 0;
    };

    /** Open a CPG handle.
     *@param handler for CPG events.
     */
    Cpg(Handler&);
    
    /** Destructor calls shutdown. */
    ~Cpg();

    /** Disconnect from CPG */
    void shutdown();
    
    /** Dispatch CPG events.
     *@param type one of
     * - CPG_DISPATCH_ONE - dispatch exactly one event.
     * - CPG_DISPATCH_ALL - dispatch all available events, don't wait.
     * - CPG_DISPATCH_BLOCKING - blocking dispatch loop.
     */
    void dispatch(cpg_dispatch_t type) {
        check(cpg_dispatch(handle,type), "Error in CPG dispatch");
    }

    void dispatchOne() { dispatch(CPG_DISPATCH_ONE); }
    void dispatchAll() { dispatch(CPG_DISPATCH_ALL); }
    void dispatchBlocking() { dispatch(CPG_DISPATCH_BLOCKING); }

    void join(const Name& group) {
        check(cpg_join(handle, const_cast<Name*>(&group)),cantJoinMsg(group));
    };
    
    void leave(const Name& group) {
        check(cpg_leave(handle,const_cast<Name*>(&group)),cantLeaveMsg(group));
    }

    void mcast(const Name& group, const iovec* iov, int iovLen) {
        check(cpg_mcast_joined(
                  handle, CPG_TYPE_AGREED, const_cast<iovec*>(iov), iovLen),
              cantMcastMsg(group));
    }

    cpg_handle_t getHandle() const { return handle; }

  private:
    class Handles;
    struct ClearHandleOnExit;
  friend class Handles;
  friend struct ClearHandleOnExit;
    
    static std::string errorStr(cpg_error_t err, const std::string& msg);
    static std::string cantJoinMsg(const Name&);
    static std::string cantLeaveMsg(const Name&);
    static std::string cantMcastMsg(const Name&);
    
    static void check(cpg_error_t result, const std::string& msg) {
        // TODO aconway 2007-06-01: Logging and exceptions.
        if (result != CPG_OK) 
            throw Exception(errorStr(result, msg));
    }

    static void globalDeliver(
        cpg_handle_t handle,
        struct cpg_name *group,
        uint32_t nodeid,
        uint32_t pid,
        void* msg,
        int msg_len);

    static void globalConfigChange(
        cpg_handle_t handle,
        struct cpg_name *group,
        struct cpg_address *members, int nMembers,
        struct cpg_address *left, int nLeft,
        struct cpg_address *joined, int nJoined
    );

    static Handles handles;
    cpg_handle_t handle;
    Handler& handler;
};

std::ostream& operator <<(std::ostream& out, const cpg_name& name);
std::ostream& operator <<(std::ostream& out, const Cpg::Id& id);
std::ostream& operator <<(std::ostream& out, const std::pair<cpg_address*,int> addresses);

inline bool operator==(const cpg_name& a, const cpg_name& b) {
    return a.length==b.length &&  strncmp(a.value, b.value, a.length) == 0;
}
inline bool operator!=(const cpg_name& a, const cpg_name& b) { return !(a == b); }

}} // namespace qpid::cluster



#endif  /*!CPG_H*/
