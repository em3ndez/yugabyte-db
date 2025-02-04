//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
//
// This module defines the API to execute QL builtin functions. It processes OP_BFCALL in an
// expression tree of QLValues.
// - See file "ql_protocol.proto" for definitions of operators (OP_BFCALL).
// - During compilation or first execution, a treenode for OP_BFCALL should be generated.
// - Either DocDB or ProxyServer should execute the node.
//
// NOTES:
// Because builtin opcodes are auto-generated during compilation, when a module includes this file,
// it must add dependency to yb_bfql library in the CMakeLists.txt to enforce that the compiler
// will generate the code for yb_bfql lib first before building the dependent library.
//   Add the following line in file CMakeLists.txt.
//   add_dependencies(<ql_bfunc.h included library> yb_bfql)
//--------------------------------------------------------------------------------------------------

#ifndef YB_COMMON_QL_BFUNC_H_
#define YB_COMMON_QL_BFUNC_H_

#include <vector>
#include <unordered_set>

#include "yb/bfpg/gen_opcodes.h"

#include "yb/bfql/gen_opcodes.h"

#include "yb/common/common_fwd.h"

#include "yb/util/status_fwd.h"

namespace yb {

//--------------------------------------------------------------------------------------------------
// CQL support
// QLBfunc defines a set of static functions to execute OP_BFUNC in QLValue expression tree.
// NOTE:
// - OP_BFUNC is not yet defined or generated.
// - Do not add non-static members to this class as QLBfunc is not meant for creating different
//   objects with different behaviors. For compability reason, all builtin calls must be processed
//   the same way across all processes and all releases in YugaByte.
CHECKED_STATUS ExecBfunc(
    bfql::BFOpcode opcode, std::vector<QLValuePB>* params, QLValuePB* result);

//--------------------------------------------------------------------------------------------------
// PGSQL support
CHECKED_STATUS ExecBfunc(
    bfpg::BFOpcode opcode, std::vector<QLValuePB>* params, QLValuePB *result);

CHECKED_STATUS ExecBfunc(
    bfpg::BFOpcode opcode, std::vector<LWQLValuePB>* params, LWQLValuePB *result);

} // namespace yb

#endif // YB_COMMON_QL_BFUNC_H_
