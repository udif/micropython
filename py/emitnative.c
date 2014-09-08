/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Essentially normal Python has 1 type: Python objects
// Viper has more than 1 type, and is just a more complicated (a superset of) Python.
// If you declare everything in Viper as a Python object (ie omit type decls) then
// it should in principle be exactly the same as Python native.
// Having types means having more opcodes, like binary_op_nat_nat, binary_op_nat_obj etc.
// In practice we won't have a VM but rather do this in asm which is actually very minimal.

// Because it breaks strict Python equivalence it should be a completely separate
// decorator.  It breaks equivalence because overflow on integers wraps around.
// It shouldn't break equivalence if you don't use the new types, but since the
// type decls might be used in normal Python for other reasons, it's probably safest,
// cleanest and clearest to make it a separate decorator.

// Actually, it does break equivalence because integers default to native integers,
// not Python objects.

// for x in l[0:8]: can be compiled into a native loop if l has pointer type

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mpconfig.h"
#include "nlr.h"
#include "misc.h"
#include "qstr.h"
#include "lexer.h"
#include "parse.h"
#include "obj.h"
#include "emitglue.h"
#include "scope.h"
#include "runtime0.h"
#include "emit.h"
#include "runtime.h"

#if 0 // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_printf(...) (void)0
#endif

// wrapper around everything in this file
#if (MICROPY_EMIT_X64 && N_X64) \
    || (MICROPY_EMIT_X86 && N_X86) \
    || (MICROPY_EMIT_THUMB && N_THUMB) \
    || (MICROPY_EMIT_ARM && N_ARM)

#if N_X64

// x64 specific stuff

#include "asmx64.h"

#define EXPORT_FUN(name) emit_native_x64_##name

#define REG_RET REG_RAX
#define REG_ARG_1 REG_RDI
#define REG_ARG_2 REG_RSI
#define REG_ARG_3 REG_RDX
#define REG_ARG_4 REG_RCX

// caller-save
#define REG_TEMP0 REG_RAX
#define REG_TEMP1 REG_RDI
#define REG_TEMP2 REG_RSI

// callee-save
#define REG_LOCAL_1 REG_RBX
#define REG_LOCAL_2 REG_R12
#define REG_LOCAL_3 REG_R13
#define REG_LOCAL_NUM (3)

#define ASM_PASS_COMPUTE    ASM_X64_PASS_COMPUTE
#define ASM_PASS_EMIT       ASM_X64_PASS_EMIT

#define ASM_T               asm_x64_t
#define ASM_NEW             asm_x64_new
#define ASM_FREE            asm_x64_free
#define ASM_GET_CODE        asm_x64_get_code
#define ASM_GET_CODE_SIZE   asm_x64_get_code_size
#define ASM_START_PASS      asm_x64_start_pass
#define ASM_END_PASS        asm_x64_end_pass
#define ASM_ENTRY           asm_x64_entry
#define ASM_EXIT            asm_x64_exit

#define ASM_LABEL_ASSIGN    asm_x64_label_assign
#define ASM_JUMP            asm_x64_jmp_label
#define ASM_JUMP_IF_REG_ZERO(as, reg, label) \
    do { \
        asm_x64_test_r8_with_r8(as, reg, reg); \
        asm_x64_jcc_label(as, ASM_X64_CC_JZ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label) \
    do { \
        asm_x64_test_r8_with_r8(as, reg, reg); \
        asm_x64_jcc_label(as, ASM_X64_CC_JNZ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) \
    do { \
        asm_x64_cmp_r64_with_r64(as, reg1, reg2); \
        asm_x64_jcc_label(as, ASM_X64_CC_JE, label); \
    } while (0)
#define ASM_CALL_IND(as, ptr, idx) asm_x64_call_ind(as, ptr, REG_RAX)

#define ASM_MOV_REG_TO_LOCAL        asm_x64_mov_r64_to_local
#define ASM_MOV_IMM_TO_REG          asm_x64_mov_i64_to_r64_optimised
#define ASM_MOV_ALIGNED_IMM_TO_REG  asm_x64_mov_i64_to_r64_aligned
#define ASM_MOV_IMM_TO_LOCAL_USING(as, imm, local_num, reg_temp) \
    do { \
        asm_x64_mov_i64_to_r64_optimised(as, (imm), (reg_temp)); \
        asm_x64_mov_r64_to_local(as, (reg_temp), (local_num)); \
    } while (false)
#define ASM_MOV_LOCAL_TO_REG        asm_x64_mov_local_to_r64
#define ASM_MOV_REG_TO_REG          asm_x64_mov_r64_to_r64
#define ASM_MOV_LOCAL_ADDR_TO_REG   asm_x64_mov_local_addr_to_r64

#elif N_X86

// x86 specific stuff

#include "asmx86.h"

STATIC byte mp_f_n_args[MP_F_NUMBER_OF] = {
    [MP_F_CONVERT_OBJ_TO_NATIVE] = 2,
    [MP_F_CONVERT_NATIVE_TO_OBJ] = 2,
    [MP_F_LOAD_CONST_INT] = 1,
    [MP_F_LOAD_CONST_DEC] = 1,
    [MP_F_LOAD_CONST_STR] = 1,
    [MP_F_LOAD_CONST_BYTES] = 1,
    [MP_F_LOAD_NAME] = 1,
    [MP_F_LOAD_GLOBAL] = 1,
    [MP_F_LOAD_BUILD_CLASS] = 0,
    [MP_F_LOAD_ATTR] = 2,
    [MP_F_LOAD_METHOD] = 3,
    [MP_F_STORE_NAME] = 2,
    [MP_F_STORE_GLOBAL] = 2,
    [MP_F_STORE_ATTR] = 3,
    [MP_F_OBJ_SUBSCR] = 3,
    [MP_F_OBJ_IS_TRUE] = 1,
    [MP_F_UNARY_OP] = 2,
    [MP_F_BINARY_OP] = 3,
    [MP_F_BUILD_TUPLE] = 2,
    [MP_F_BUILD_LIST] = 2,
    [MP_F_LIST_APPEND] = 2,
    [MP_F_BUILD_MAP] = 1,
    [MP_F_STORE_MAP] = 3,
#if MICROPY_PY_BUILTINS_SET
    [MP_F_BUILD_SET] = 2,
    [MP_F_STORE_SET] = 2,
#endif
    [MP_F_MAKE_FUNCTION_FROM_RAW_CODE] = 3,
    [MP_F_NATIVE_CALL_FUNCTION_N_KW] = 3,
    [MP_F_CALL_METHOD_N_KW] = 3,
    [MP_F_GETITER] = 1,
    [MP_F_ITERNEXT] = 1,
    [MP_F_NLR_PUSH] = 1,
    [MP_F_NLR_POP] = 0,
    [MP_F_NATIVE_RAISE] = 1,
    [MP_F_IMPORT_NAME] = 3,
    [MP_F_IMPORT_FROM] = 2,
    [MP_F_IMPORT_ALL] = 1,
#if MICROPY_PY_BUILTINS_SLICE
    [MP_F_NEW_SLICE] = 3,
#endif
    [MP_F_UNPACK_SEQUENCE] = 3,
    [MP_F_UNPACK_EX] = 3,
    [MP_F_DELETE_NAME] = 1,
    [MP_F_DELETE_GLOBAL] = 1,
};

#define EXPORT_FUN(name) emit_native_x86_##name

#define REG_RET REG_EAX
#define REG_ARG_1 REG_EAX
#define REG_ARG_2 REG_ECX
#define REG_ARG_3 REG_EDX

// caller-save, so can be used as temporaries
#define REG_TEMP0 REG_EAX
#define REG_TEMP1 REG_ECX
#define REG_TEMP2 REG_EDX

// callee-save, so can be used as locals
#define REG_LOCAL_1 REG_EBX
#define REG_LOCAL_2 REG_ESI
#define REG_LOCAL_3 REG_EDI
#define REG_LOCAL_NUM (3)

#define ASM_PASS_COMPUTE    ASM_X86_PASS_COMPUTE
#define ASM_PASS_EMIT       ASM_X86_PASS_EMIT

#define ASM_T               asm_x86_t
#define ASM_NEW             asm_x86_new
#define ASM_FREE            asm_x86_free
#define ASM_GET_CODE        asm_x86_get_code
#define ASM_GET_CODE_SIZE   asm_x86_get_code_size
#define ASM_START_PASS      asm_x86_start_pass
#define ASM_END_PASS        asm_x86_end_pass
#define ASM_ENTRY           asm_x86_entry
#define ASM_EXIT            asm_x86_exit

#define ASM_LABEL_ASSIGN    asm_x86_label_assign
#define ASM_JUMP            asm_x86_jmp_label
#define ASM_JUMP_IF_REG_ZERO(as, reg, label) \
    do { \
        asm_x86_test_r8_with_r8(as, reg, reg); \
        asm_x86_jcc_label(as, ASM_X86_CC_JZ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label) \
    do { \
        asm_x86_test_r8_with_r8(as, reg, reg); \
        asm_x86_jcc_label(as, ASM_X86_CC_JNZ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) \
    do { \
        asm_x86_cmp_r32_with_r32(as, reg1, reg2); \
        asm_x86_jcc_label(as, ASM_X86_CC_JE, label); \
    } while (0)
#define ASM_CALL_IND(as, ptr, idx) asm_x86_call_ind(as, ptr, mp_f_n_args[idx], REG_EAX)

#define ASM_MOV_REG_TO_LOCAL        asm_x86_mov_r32_to_local
#define ASM_MOV_IMM_TO_REG          asm_x86_mov_i32_to_r32
#define ASM_MOV_ALIGNED_IMM_TO_REG  asm_x86_mov_i32_to_r32_aligned
#define ASM_MOV_IMM_TO_LOCAL_USING(as, imm, local_num, reg_temp) \
    do { \
        asm_x86_mov_i32_to_r32(as, (imm), (reg_temp)); \
        asm_x86_mov_r32_to_local(as, (reg_temp), (local_num)); \
    } while (false)
#define ASM_MOV_LOCAL_TO_REG        asm_x86_mov_local_to_r32
#define ASM_MOV_REG_TO_REG          asm_x86_mov_r32_to_r32
#define ASM_MOV_LOCAL_ADDR_TO_REG   asm_x86_mov_local_addr_to_r32

#elif N_THUMB

// thumb specific stuff

#include "asmthumb.h"

#define EXPORT_FUN(name) emit_native_thumb_##name

#define REG_RET REG_R0
#define REG_ARG_1 REG_R0
#define REG_ARG_2 REG_R1
#define REG_ARG_3 REG_R2
#define REG_ARG_4 REG_R3

#define REG_TEMP0 (REG_R0)
#define REG_TEMP1 (REG_R1)
#define REG_TEMP2 (REG_R2)

#define REG_LOCAL_1 (REG_R4)
#define REG_LOCAL_2 (REG_R5)
#define REG_LOCAL_3 (REG_R6)
#define REG_LOCAL_NUM (3)

#define ASM_PASS_COMPUTE    ASM_THUMB_PASS_COMPUTE
#define ASM_PASS_EMIT       ASM_THUMB_PASS_EMIT

#define ASM_T               asm_thumb_t
#define ASM_NEW             asm_thumb_new
#define ASM_FREE            asm_thumb_free
#define ASM_GET_CODE        asm_thumb_get_code
#define ASM_GET_CODE_SIZE   asm_thumb_get_code_size
#define ASM_START_PASS      asm_thumb_start_pass
#define ASM_END_PASS        asm_thumb_end_pass
#define ASM_ENTRY           asm_thumb_entry
#define ASM_EXIT            asm_thumb_exit

#define ASM_LABEL_ASSIGN    asm_thumb_label_assign
#define ASM_JUMP            asm_thumb_b_label
#define ASM_JUMP_IF_REG_ZERO(as, reg, label) \
    do { \
        asm_thumb_cmp_rlo_i8(as, reg, 0); \
        asm_thumb_bcc_label(as, THUMB_CC_EQ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label) \
    do { \
        asm_thumb_cmp_rlo_i8(as, reg, 0); \
        asm_thumb_bcc_label(as, THUMB_CC_NE, label); \
    } while (0)
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) \
    do { \
        asm_thumb_cmp_rlo_rlo(as, reg1, reg2); \
        asm_thumb_bcc_label(as, THUMB_CC_EQ, label); \
    } while (0)
#define ASM_CALL_IND(as, ptr, idx) asm_thumb_bl_ind(as, ptr, idx, REG_R3)

#define ASM_MOV_REG_TO_LOCAL(as, reg, local_num) asm_thumb_mov_local_reg(as, (local_num), (reg))
#define ASM_MOV_IMM_TO_REG(as, imm, reg) asm_thumb_mov_reg_i32_optimised(as, (reg), (imm))
#define ASM_MOV_ALIGNED_IMM_TO_REG(as, imm, reg) asm_thumb_mov_reg_i32_aligned(as, (reg), (imm))
#define ASM_MOV_IMM_TO_LOCAL_USING(as, imm, local_num, reg_temp) \
    do { \
        asm_thumb_mov_reg_i32_optimised(as, (reg_temp), (imm)); \
        asm_thumb_mov_local_reg(as, (local_num), (reg_temp)); \
    } while (false)
#define ASM_MOV_LOCAL_TO_REG(as, local_num, reg) asm_thumb_mov_reg_local(as, (reg), (local_num))
#define ASM_MOV_REG_TO_REG(as, reg_src, reg_dest) asm_thumb_mov_reg_reg(as, (reg_dest), (reg_src))
#define ASM_MOV_LOCAL_ADDR_TO_REG(as, local_num, reg) asm_thumb_mov_reg_local_addr(as, (reg), (local_num))

#elif N_ARM

// ARM specific stuff

#include "asmarm.h"

#define EXPORT_FUN(name) emit_native_arm_##name

#define REG_RET REG_R0
#define REG_ARG_1 REG_R0
#define REG_ARG_2 REG_R1
#define REG_ARG_3 REG_R2
#define REG_ARG_4 REG_R3

#define REG_TEMP0 (REG_R0)
#define REG_TEMP1 (REG_R1)
#define REG_TEMP2 (REG_R2)

#define REG_LOCAL_1 (REG_R4)
#define REG_LOCAL_2 (REG_R5)
#define REG_LOCAL_3 (REG_R6)
#define REG_LOCAL_NUM (3)

#define ASM_PASS_COMPUTE    ASM_ARM_PASS_COMPUTE
#define ASM_PASS_EMIT       ASM_ARM_PASS_EMIT

#define ASM_T               asm_arm_t
#define ASM_NEW             asm_arm_new
#define ASM_FREE            asm_arm_free
#define ASM_GET_CODE        asm_arm_get_code
#define ASM_GET_CODE_SIZE   asm_arm_get_code_size
#define ASM_START_PASS      asm_arm_start_pass
#define ASM_END_PASS        asm_arm_end_pass
#define ASM_ENTRY           asm_arm_entry
#define ASM_EXIT            asm_arm_exit

#define ASM_LABEL_ASSIGN    asm_arm_label_assign
#define ASM_JUMP            asm_arm_b_label
#define ASM_JUMP_IF_REG_ZERO(as, reg, label) \
    do { \
        asm_arm_cmp_reg_i8(as, reg, 0); \
        asm_arm_bcc_label(as, ARM_CC_EQ, label); \
    } while (0)
#define ASM_JUMP_IF_REG_NONZERO(as, reg, label) \
    do { \
        asm_arm_cmp_reg_i8(as, reg, 0); \
        asm_arm_bcc_label(as, ARM_CC_NE, label); \
    } while (0)
#define ASM_JUMP_IF_REG_EQ(as, reg1, reg2, label) \
    do { \
        asm_arm_cmp_reg_reg(as, reg1, reg2); \
        asm_arm_bcc_label(as, ARM_CC_EQ, label); \
    } while (0)
#define ASM_CALL_IND(as, ptr, idx) asm_arm_bl_ind(as, ptr, idx, REG_R3)

#define ASM_MOV_REG_TO_LOCAL(as, reg, local_num) asm_arm_mov_local_reg(as, (local_num), (reg))
#define ASM_MOV_IMM_TO_REG(as, imm, reg) asm_arm_mov_reg_i32(as, (reg), (imm))
#define ASM_MOV_ALIGNED_IMM_TO_REG(as, imm, reg) asm_arm_mov_reg_i32(as, (reg), (imm))
#define ASM_MOV_IMM_TO_LOCAL_USING(as, imm, local_num, reg_temp) \
    do { \
        asm_arm_mov_reg_i32(as, (reg_temp), (imm)); \
        asm_arm_mov_local_reg(as, (local_num), (reg_temp)); \
    } while (false)
#define ASM_MOV_LOCAL_TO_REG(as, local_num, reg) asm_arm_mov_reg_local(as, (reg), (local_num))
#define ASM_MOV_REG_TO_REG(as, reg_src, reg_dest) asm_arm_mov_reg_reg(as, (reg_dest), (reg_src))
#define ASM_MOV_LOCAL_ADDR_TO_REG(as, local_num, reg) asm_arm_mov_reg_local_addr(as, (reg), (local_num))

#else

#error unknown native emitter

#endif

typedef enum {
    STACK_VALUE,
    STACK_REG,
    STACK_IMM,
} stack_info_kind_t;

typedef enum {
    VTYPE_PYOBJ = MP_NATIVE_TYPE_OBJ,
    VTYPE_BOOL = MP_NATIVE_TYPE_BOOL,
    VTYPE_INT = MP_NATIVE_TYPE_INT,
    VTYPE_UINT = MP_NATIVE_TYPE_UINT,
    VTYPE_UNBOUND,
    VTYPE_PTR,
    VTYPE_PTR_NONE,
    VTYPE_BUILTIN_V_INT,
} vtype_kind_t;

typedef struct _stack_info_t {
    vtype_kind_t vtype;
    stack_info_kind_t kind;
    union {
        int u_reg;
        mp_int_t u_imm;
    };
} stack_info_t;

struct _emit_t {
    int pass;

    bool do_viper_types;

    vtype_kind_t return_vtype;

    mp_uint_t local_vtype_alloc;
    vtype_kind_t *local_vtype;

    mp_uint_t stack_info_alloc;
    stack_info_t *stack_info;

    int stack_start;
    int stack_size;

    bool last_emit_was_return_value;

    scope_t *scope;

    ASM_T *as;
};

emit_t *EXPORT_FUN(new)(mp_uint_t max_num_labels) {
    emit_t *emit = m_new0(emit_t, 1);
    emit->as = ASM_NEW(max_num_labels);
    return emit;
}

void EXPORT_FUN(free)(emit_t *emit) {
    ASM_FREE(emit->as, false);
    m_del(vtype_kind_t, emit->local_vtype, emit->local_vtype_alloc);
    m_del(stack_info_t, emit->stack_info, emit->stack_info_alloc);
    m_del_obj(emit_t, emit);
}

STATIC void emit_native_set_native_type(emit_t *emit, mp_uint_t op, mp_uint_t arg1, qstr arg2) {
    switch (op) {
        case MP_EMIT_NATIVE_TYPE_ENABLE:
            emit->do_viper_types = arg1;
            break;

        default: {
            vtype_kind_t type;
            switch (arg2) {
                case MP_QSTR_object: type = VTYPE_PYOBJ; break;
                case MP_QSTR_bool: type = VTYPE_BOOL; break;
                case MP_QSTR_int: type = VTYPE_INT; break;
                case MP_QSTR_uint: type = VTYPE_UINT; break;
                default: printf("ViperTypeError: unknown type %s\n", qstr_str(arg2)); return;
            }
            if (op == MP_EMIT_NATIVE_TYPE_RETURN) {
                emit->return_vtype = type;
            } else {
                assert(arg1 < emit->local_vtype_alloc);
                emit->local_vtype[arg1] = type;
            }
            break;
        }
    }
}

STATIC void emit_native_start_pass(emit_t *emit, pass_kind_t pass, scope_t *scope) {
    emit->pass = pass;
    emit->stack_start = 0;
    emit->stack_size = 0;
    emit->last_emit_was_return_value = false;
    emit->scope = scope;

    // allocate memory for keeping track of the types of locals
    if (emit->local_vtype_alloc < scope->num_locals) {
        emit->local_vtype = m_renew(vtype_kind_t, emit->local_vtype, emit->local_vtype_alloc, scope->num_locals);
        emit->local_vtype_alloc = scope->num_locals;
    }

    // allocate memory for keeping track of the objects on the stack
    // XXX don't know stack size on entry, and it should be maximum over all scopes
    if (emit->stack_info == NULL) {
        emit->stack_info_alloc = scope->stack_size + 50;
        emit->stack_info = m_new(stack_info_t, emit->stack_info_alloc);
    }

    // set default type for return and arguments
    emit->return_vtype = VTYPE_PYOBJ;
    for (mp_uint_t i = 0; i < emit->scope->num_pos_args; i++) {
        emit->local_vtype[i] = VTYPE_PYOBJ;
    }

    // local variables begin unbound, and have unknown type
    for (mp_uint_t i = emit->scope->num_pos_args; i < emit->local_vtype_alloc; i++) {
        emit->local_vtype[i] = VTYPE_UNBOUND;
    }

    // values on stack begin unbound
    for (mp_uint_t i = 0; i < emit->stack_info_alloc; i++) {
        emit->stack_info[i].kind = STACK_VALUE;
        emit->stack_info[i].vtype = VTYPE_UNBOUND;
    }

    ASM_START_PASS(emit->as, pass == MP_PASS_EMIT ? ASM_PASS_EMIT : ASM_PASS_COMPUTE);

    // entry to function
    int num_locals = 0;
    if (pass > MP_PASS_SCOPE) {
        num_locals = scope->num_locals - REG_LOCAL_NUM;
        if (num_locals < 0) {
            num_locals = 0;
        }
        emit->stack_start = num_locals;
        num_locals += scope->stack_size;
    }
    ASM_ENTRY(emit->as, num_locals);

    // initialise locals from parameters
#if N_X64
    for (int i = 0; i < scope->num_pos_args; i++) {
        if (i == 0) {
            asm_x64_mov_r64_to_r64(emit->as, REG_ARG_1, REG_LOCAL_1);
        } else if (i == 1) {
            asm_x64_mov_r64_to_r64(emit->as, REG_ARG_2, REG_LOCAL_2);
        } else if (i == 2) {
            asm_x64_mov_r64_to_r64(emit->as, REG_ARG_3, REG_LOCAL_3);
        } else if (i == 3) {
            asm_x64_mov_r64_to_local(emit->as, REG_ARG_4, i - REG_LOCAL_NUM);
        } else {
            // TODO not implemented
            assert(0);
        }
    }
#elif N_X86
    for (int i = 0; i < scope->num_pos_args; i++) {
        if (i == 0) {
            asm_x86_mov_arg_to_r32(emit->as, i, REG_LOCAL_1);
        } else if (i == 1) {
            asm_x86_mov_arg_to_r32(emit->as, i, REG_LOCAL_2);
        } else if (i == 2) {
            asm_x86_mov_arg_to_r32(emit->as, i, REG_LOCAL_3);
        } else {
            asm_x86_mov_arg_to_r32(emit->as, i, REG_TEMP0);
            asm_x86_mov_r32_to_local(emit->as, REG_TEMP0, i - REG_LOCAL_NUM);
        }
    }
#elif N_THUMB
    for (int i = 0; i < scope->num_pos_args; i++) {
        if (i == 0) {
            asm_thumb_mov_reg_reg(emit->as, REG_LOCAL_1, REG_ARG_1);
        } else if (i == 1) {
            asm_thumb_mov_reg_reg(emit->as, REG_LOCAL_2, REG_ARG_2);
        } else if (i == 2) {
            asm_thumb_mov_reg_reg(emit->as, REG_LOCAL_3, REG_ARG_3);
        } else if (i == 3) {
            asm_thumb_mov_local_reg(emit->as, i - REG_LOCAL_NUM, REG_ARG_4);
        } else {
            // TODO not implemented
            assert(0);
        }
    }

    asm_thumb_mov_reg_i32(emit->as, REG_R7, (mp_uint_t)mp_fun_table);
#elif N_ARM
    for (int i = 0; i < scope->num_pos_args; i++) {
        if (i == 0) {
            asm_arm_mov_reg_reg(emit->as, REG_LOCAL_1, REG_ARG_1);
        } else if (i == 1) {
            asm_arm_mov_reg_reg(emit->as, REG_LOCAL_2, REG_ARG_2);
        } else if (i == 2) {
            asm_arm_mov_reg_reg(emit->as, REG_LOCAL_3, REG_ARG_3);
        } else if (i == 3) {
            asm_arm_mov_local_reg(emit->as, i - REG_LOCAL_NUM, REG_ARG_4);
        } else {
            // TODO not implemented
            assert(0);
        }
    }

    asm_arm_mov_reg_i32(emit->as, REG_R7, (mp_uint_t)mp_fun_table);
#else
    #error not implemented
#endif
}

STATIC void emit_native_end_pass(emit_t *emit) {
    if (!emit->last_emit_was_return_value) {
        ASM_EXIT(emit->as);
    }
    ASM_END_PASS(emit->as);

    // check stack is back to zero size
    if (emit->stack_size != 0) {
        printf("ERROR: stack size not back to zero; got %d\n", emit->stack_size);
    }

    if (emit->pass == MP_PASS_EMIT) {
        void *f = ASM_GET_CODE(emit->as);
        mp_uint_t f_len = ASM_GET_CODE_SIZE(emit->as);

        // compute type signature
        // TODO check that viper types here convert correctly to valid types for emit glue
        mp_uint_t type_sig = emit->return_vtype & 3;
        for (mp_uint_t i = 0; i < emit->scope->num_pos_args; i++) {
            type_sig |= (emit->local_vtype[i] & 3) << (i * 2 + 2);
        }

        mp_emit_glue_assign_native(emit->scope->raw_code, emit->do_viper_types ? MP_CODE_NATIVE_VIPER : MP_CODE_NATIVE_PY, f, f_len, emit->scope->num_pos_args, type_sig);
    }
}

STATIC bool emit_native_last_emit_was_return_value(emit_t *emit) {
    return emit->last_emit_was_return_value;
}

STATIC void emit_native_adjust_stack_size(emit_t *emit, mp_int_t delta) {
    emit->stack_size += delta;
}

STATIC void emit_native_set_source_line(emit_t *emit, mp_uint_t source_line) {
}

STATIC void adjust_stack(emit_t *emit, int stack_size_delta) {
    DEBUG_printf("adjust stack: stack:%d + delta:%d\n", emit->stack_size, stack_size_delta);
    assert((int)emit->stack_size + stack_size_delta >= 0);
    emit->stack_size += stack_size_delta;
    if (emit->pass > MP_PASS_SCOPE && emit->stack_size > emit->scope->stack_size) {
        emit->scope->stack_size = emit->stack_size;
    }
}

/*
STATIC void emit_pre_raw(emit_t *emit, int stack_size_delta) {
    adjust_stack(emit, stack_size_delta);
    emit->last_emit_was_return_value = false;
}
*/

// this must be called at start of emit functions
STATIC void emit_native_pre(emit_t *emit) {
    emit->last_emit_was_return_value = false;
    // settle the stack
    /*
    if (regs_needed != 0) {
        for (int i = 0; i < emit->stack_size; i++) {
            switch (emit->stack_info[i].kind) {
                case STACK_VALUE:
                    break;

                case STACK_REG:
                    // TODO only push reg if in regs_needed
                    emit->stack_info[i].kind = STACK_VALUE;
                    ASM_MOV_REG_TO_LOCAL(emit->as, emit->stack_info[i].u_reg, emit->stack_start + i);
                    break;

                case STACK_IMM:
                    // don't think we ever need to push imms for settling
                    //ASM_MOV_IMM_TO_LOCAL(emit->last_imm, emit->stack_start + i);
                    break;
            }
        }
    }
    */
}

STATIC vtype_kind_t peek_vtype(emit_t *emit) {
    return emit->stack_info[emit->stack_size - 1].vtype;
}

// pos=1 is TOS, pos=2 is next, etc
// use pos=0 for no skipping
STATIC void need_reg_single(emit_t *emit, int reg_needed, int skip_stack_pos) {
    skip_stack_pos = emit->stack_size - skip_stack_pos;
    for (int i = 0; i < emit->stack_size; i++) {
        if (i != skip_stack_pos) {
            stack_info_t *si = &emit->stack_info[i];
            if (si->kind == STACK_REG && si->u_reg == reg_needed) {
                si->kind = STACK_VALUE;
                ASM_MOV_REG_TO_LOCAL(emit->as, si->u_reg, emit->stack_start + i);
            }
        }
    }
}

STATIC void need_reg_all(emit_t *emit) {
    for (int i = 0; i < emit->stack_size; i++) {
        stack_info_t *si = &emit->stack_info[i];
        if (si->kind == STACK_REG) {
            si->kind = STACK_VALUE;
            ASM_MOV_REG_TO_LOCAL(emit->as, si->u_reg, emit->stack_start + i);
        }
    }
}

STATIC void need_stack_settled(emit_t *emit) {
    for (int i = 0; i < emit->stack_size; i++) {
        stack_info_t *si = &emit->stack_info[i];
        if (si->kind == STACK_REG) {
            si->kind = STACK_VALUE;
            ASM_MOV_REG_TO_LOCAL(emit->as, si->u_reg, emit->stack_start + i);
        }
    }
    for (int i = 0; i < emit->stack_size; i++) {
        stack_info_t *si = &emit->stack_info[i];
        if (si->kind == STACK_IMM) {
            si->kind = STACK_VALUE;
            ASM_MOV_IMM_TO_LOCAL_USING(emit->as, si->u_imm, emit->stack_start + i, REG_TEMP0);
        }
    }
}

// pos=1 is TOS, pos=2 is next, etc
STATIC void emit_access_stack(emit_t *emit, int pos, vtype_kind_t *vtype, int reg_dest) {
    need_reg_single(emit, reg_dest, pos);
    stack_info_t *si = &emit->stack_info[emit->stack_size - pos];
    *vtype = si->vtype;
    switch (si->kind) {
        case STACK_VALUE:
            ASM_MOV_LOCAL_TO_REG(emit->as, emit->stack_start + emit->stack_size - pos, reg_dest);
            break;

        case STACK_REG:
            if (si->u_reg != reg_dest) {
                ASM_MOV_REG_TO_REG(emit->as, si->u_reg, reg_dest);
            }
            break;

        case STACK_IMM:
            ASM_MOV_IMM_TO_REG(emit->as, si->u_imm, reg_dest);
            break;
    }
}

STATIC void emit_pre_pop_discard(emit_t *emit) {
    emit->last_emit_was_return_value = false;
    adjust_stack(emit, -1);
}

STATIC void emit_pre_pop_reg(emit_t *emit, vtype_kind_t *vtype, int reg_dest) {
    emit->last_emit_was_return_value = false;
    emit_access_stack(emit, 1, vtype, reg_dest);
    adjust_stack(emit, -1);
}

STATIC void emit_pre_pop_reg_reg(emit_t *emit, vtype_kind_t *vtypea, int rega, vtype_kind_t *vtypeb, int regb) {
    emit_pre_pop_reg(emit, vtypea, rega);
    emit_pre_pop_reg(emit, vtypeb, regb);
}

STATIC void emit_pre_pop_reg_reg_reg(emit_t *emit, vtype_kind_t *vtypea, int rega, vtype_kind_t *vtypeb, int regb, vtype_kind_t *vtypec, int regc) {
    emit_pre_pop_reg(emit, vtypea, rega);
    emit_pre_pop_reg(emit, vtypeb, regb);
    emit_pre_pop_reg(emit, vtypec, regc);
}

STATIC void emit_post(emit_t *emit) {
}

STATIC void emit_post_push_reg(emit_t *emit, vtype_kind_t vtype, int reg) {
    stack_info_t *si = &emit->stack_info[emit->stack_size];
    si->vtype = vtype;
    si->kind = STACK_REG;
    si->u_reg = reg;
    adjust_stack(emit, 1);
}

STATIC void emit_post_push_imm(emit_t *emit, vtype_kind_t vtype, mp_int_t imm) {
    stack_info_t *si = &emit->stack_info[emit->stack_size];
    si->vtype = vtype;
    si->kind = STACK_IMM;
    si->u_imm = imm;
    adjust_stack(emit, 1);
}

STATIC void emit_post_push_reg_reg(emit_t *emit, vtype_kind_t vtypea, int rega, vtype_kind_t vtypeb, int regb) {
    emit_post_push_reg(emit, vtypea, rega);
    emit_post_push_reg(emit, vtypeb, regb);
}

STATIC void emit_post_push_reg_reg_reg(emit_t *emit, vtype_kind_t vtypea, int rega, vtype_kind_t vtypeb, int regb, vtype_kind_t vtypec, int regc) {
    emit_post_push_reg(emit, vtypea, rega);
    emit_post_push_reg(emit, vtypeb, regb);
    emit_post_push_reg(emit, vtypec, regc);
}

STATIC void emit_post_push_reg_reg_reg_reg(emit_t *emit, vtype_kind_t vtypea, int rega, vtype_kind_t vtypeb, int regb, vtype_kind_t vtypec, int regc, vtype_kind_t vtyped, int regd) {
    emit_post_push_reg(emit, vtypea, rega);
    emit_post_push_reg(emit, vtypeb, regb);
    emit_post_push_reg(emit, vtypec, regc);
    emit_post_push_reg(emit, vtyped, regd);
}

STATIC void emit_call(emit_t *emit, mp_fun_kind_t fun_kind) {
    need_reg_all(emit);
    ASM_CALL_IND(emit->as, mp_fun_table[fun_kind], fun_kind);
}

STATIC void emit_call_with_imm_arg(emit_t *emit, mp_fun_kind_t fun_kind, mp_int_t arg_val, int arg_reg) {
    need_reg_all(emit);
    ASM_MOV_IMM_TO_REG(emit->as, arg_val, arg_reg);
    ASM_CALL_IND(emit->as, mp_fun_table[fun_kind], fun_kind);
}

// the first arg is stored in the code aligned on a mp_uint_t boundary
STATIC void emit_call_with_imm_arg_aligned(emit_t *emit, mp_fun_kind_t fun_kind, mp_int_t arg_val, int arg_reg) {
    need_reg_all(emit);
    ASM_MOV_ALIGNED_IMM_TO_REG(emit->as, arg_val, arg_reg);
    ASM_CALL_IND(emit->as, mp_fun_table[fun_kind], fun_kind);
}

STATIC void emit_call_with_2_imm_args(emit_t *emit, mp_fun_kind_t fun_kind, mp_int_t arg_val1, int arg_reg1, mp_int_t arg_val2, int arg_reg2) {
    need_reg_all(emit);
    ASM_MOV_IMM_TO_REG(emit->as, arg_val1, arg_reg1);
    ASM_MOV_IMM_TO_REG(emit->as, arg_val2, arg_reg2);
    ASM_CALL_IND(emit->as, mp_fun_table[fun_kind], fun_kind);
}

// the first arg is stored in the code aligned on a mp_uint_t boundary
STATIC void emit_call_with_3_imm_args_and_first_aligned(emit_t *emit, mp_fun_kind_t fun_kind, mp_int_t arg_val1, int arg_reg1, mp_int_t arg_val2, int arg_reg2, mp_int_t arg_val3, int arg_reg3) {
    need_reg_all(emit);
    ASM_MOV_ALIGNED_IMM_TO_REG(emit->as, arg_val1, arg_reg1);
    ASM_MOV_IMM_TO_REG(emit->as, arg_val2, arg_reg2);
    ASM_MOV_IMM_TO_REG(emit->as, arg_val3, arg_reg3);
    ASM_CALL_IND(emit->as, mp_fun_table[fun_kind], fun_kind);
}

// vtype of all n_pop objects is VTYPE_PYOBJ
// Will convert any items that are not VTYPE_PYOBJ to this type and put them back on the stack.
// If any conversions of non-immediate values are needed, then it uses REG_ARG_1, REG_ARG_2 and REG_RET.
// Otherwise, it does not use any temporary registers (but may use reg_dest before loading it with stack pointer).
STATIC void emit_get_stack_pointer_to_reg_for_pop(emit_t *emit, mp_uint_t reg_dest, mp_uint_t n_pop) {
    need_reg_all(emit);

    // First, store any immediate values to their respective place on the stack.
    for (mp_uint_t i = 0; i < n_pop; i++) {
        stack_info_t *si = &emit->stack_info[emit->stack_size - 1 - i];
        // must push any imm's to stack
        // must convert them to VTYPE_PYOBJ for viper code
        if (si->kind == STACK_IMM) {
            si->kind = STACK_VALUE;
            switch (si->vtype) {
                case VTYPE_PYOBJ:
                    ASM_MOV_IMM_TO_LOCAL_USING(emit->as, si->u_imm, emit->stack_start + emit->stack_size - 1 - i, reg_dest);
                    break;
                case VTYPE_BOOL:
                    if (si->u_imm == 0) {
                        ASM_MOV_IMM_TO_LOCAL_USING(emit->as, (mp_uint_t)mp_const_false, emit->stack_start + emit->stack_size - 1 - i, reg_dest);
                    } else {
                        ASM_MOV_IMM_TO_LOCAL_USING(emit->as, (mp_uint_t)mp_const_true, emit->stack_start + emit->stack_size - 1 - i, reg_dest);
                    }
                    si->vtype = VTYPE_PYOBJ;
                    break;
                case VTYPE_INT:
                case VTYPE_UINT:
                    ASM_MOV_IMM_TO_LOCAL_USING(emit->as, (si->u_imm << 1) | 1, emit->stack_start + emit->stack_size - 1 - i, reg_dest);
                    si->vtype = VTYPE_PYOBJ;
                    break;
                default:
                    // not handled
                    assert(0);
            }
        }

        // verify that this value is on the stack
        assert(si->kind == STACK_VALUE);
    }

    // Second, convert any non-VTYPE_PYOBJ to that type.
    for (mp_uint_t i = 0; i < n_pop; i++) {
        stack_info_t *si = &emit->stack_info[emit->stack_size - 1 - i];
        if (si->vtype != VTYPE_PYOBJ) {
            mp_uint_t local_num = emit->stack_start + emit->stack_size - 1 - i;
            ASM_MOV_LOCAL_TO_REG(emit->as, local_num, REG_ARG_1);
            emit_call_with_imm_arg(emit, MP_F_CONVERT_NATIVE_TO_OBJ, si->vtype, REG_ARG_2); // arg2 = type
            ASM_MOV_REG_TO_LOCAL(emit->as, REG_RET, local_num);
            si->vtype = VTYPE_PYOBJ;
        }
    }

    // Adujust the stack for a pop of n_pop items, and load the stack pointer into reg_dest.
    adjust_stack(emit, -n_pop);
    ASM_MOV_LOCAL_ADDR_TO_REG(emit->as, emit->stack_start + emit->stack_size, reg_dest);
}

// vtype of all n_push objects is VTYPE_PYOBJ
STATIC void emit_get_stack_pointer_to_reg_for_push(emit_t *emit, mp_uint_t reg_dest, mp_uint_t n_push) {
    need_reg_all(emit);
    for (mp_uint_t i = 0; i < n_push; i++) {
        emit->stack_info[emit->stack_size + i].kind = STACK_VALUE;
        emit->stack_info[emit->stack_size + i].vtype = VTYPE_PYOBJ;
    }
    ASM_MOV_LOCAL_ADDR_TO_REG(emit->as, emit->stack_start + emit->stack_size, reg_dest);
    adjust_stack(emit, n_push);
}

STATIC void emit_native_load_id(emit_t *emit, qstr qst) {
    emit_common_load_id(emit, &EXPORT_FUN(method_table), emit->scope, qst);
}

STATIC void emit_native_store_id(emit_t *emit, qstr qst) {
    emit_common_store_id(emit, &EXPORT_FUN(method_table), emit->scope, qst);
}

STATIC void emit_native_delete_id(emit_t *emit, qstr qst) {
    emit_common_delete_id(emit, &EXPORT_FUN(method_table), emit->scope, qst);
}

STATIC void emit_native_label_assign(emit_t *emit, mp_uint_t l) {
    emit_native_pre(emit);
    // need to commit stack because we can jump here from elsewhere
    need_stack_settled(emit);
    ASM_LABEL_ASSIGN(emit->as, l);
    emit_post(emit);
}

STATIC void emit_native_import_name(emit_t *emit, qstr qst) {
    DEBUG_printf("import_name %s\n", qstr_str(qst));
    vtype_kind_t vtype_fromlist;
    vtype_kind_t vtype_level;
    emit_pre_pop_reg_reg(emit, &vtype_fromlist, REG_ARG_2, &vtype_level, REG_ARG_3); // arg2 = fromlist, arg3 = level
    assert(vtype_fromlist == VTYPE_PYOBJ);
    assert(vtype_level == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_IMPORT_NAME, qst, REG_ARG_1); // arg1 = import name
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_import_from(emit_t *emit, qstr qst) {
    DEBUG_printf("import_from %s\n", qstr_str(qst));
    emit_native_pre(emit);
    vtype_kind_t vtype_module;
    emit_access_stack(emit, 1, &vtype_module, REG_ARG_1); // arg1 = module
    assert(vtype_module == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_IMPORT_FROM, qst, REG_ARG_2); // arg2 = import name
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_import_star(emit_t *emit) {
    DEBUG_printf("import_star\n");
    vtype_kind_t vtype_module;
    emit_pre_pop_reg(emit, &vtype_module, REG_ARG_1); // arg1 = module
    assert(vtype_module == VTYPE_PYOBJ);
    emit_call(emit, MP_F_IMPORT_ALL);
    emit_post(emit);
}

STATIC void emit_native_load_const_tok(emit_t *emit, mp_token_kind_t tok) {
    DEBUG_printf("load_const_tok %d\n", tok);
    emit_native_pre(emit);
    int vtype;
    mp_uint_t val;
    if (emit->do_viper_types) {
        switch (tok) {
            case MP_TOKEN_KW_NONE: vtype = VTYPE_PTR_NONE; val = 0; break;
            case MP_TOKEN_KW_FALSE: vtype = VTYPE_BOOL; val = 0; break;
            case MP_TOKEN_KW_TRUE: vtype = VTYPE_BOOL; val = 1; break;
            default: assert(0); vtype = 0; val = 0; // shouldn't happen
        }
    } else {
        vtype = VTYPE_PYOBJ;
        switch (tok) {
            case MP_TOKEN_KW_NONE: val = (mp_uint_t)mp_const_none; break;
            case MP_TOKEN_KW_FALSE: val = (mp_uint_t)mp_const_false; break;
            case MP_TOKEN_KW_TRUE: val = (mp_uint_t)mp_const_true; break;
            default: assert(0); vtype = 0; val = 0; // shouldn't happen
        }
    }
    emit_post_push_imm(emit, vtype, val);
}

STATIC void emit_native_load_const_small_int(emit_t *emit, mp_int_t arg) {
    DEBUG_printf("load_const_small_int %d\n", arg);
    emit_native_pre(emit);
    if (emit->do_viper_types) {
        emit_post_push_imm(emit, VTYPE_INT, arg);
    } else {
        emit_post_push_imm(emit, VTYPE_PYOBJ, (arg << 1) | 1);
    }
}

STATIC void emit_native_load_const_int(emit_t *emit, qstr qst) {
    DEBUG_printf("load_const_int %s\n", qstr_str(st));
    // for viper: load integer, check fits in 32 bits
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_LOAD_CONST_INT, qst, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_const_dec(emit_t *emit, qstr qst) {
    // for viper, a float/complex is just a Python object
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_LOAD_CONST_DEC, qst, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_const_str(emit_t *emit, qstr qst, bool bytes) {
    emit_native_pre(emit);
    if (emit->do_viper_types) {
        // not implemented properly
        // load a pointer to the asciiz string?
        assert(0);
        emit_post_push_imm(emit, VTYPE_PTR, (mp_uint_t)qstr_str(qst));
    } else {
        if (bytes) {
            emit_call_with_imm_arg(emit, MP_F_LOAD_CONST_BYTES, qst, REG_ARG_1);
        } else {
            emit_call_with_imm_arg(emit, MP_F_LOAD_CONST_STR, qst, REG_ARG_1);
        }
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    }
}

STATIC void emit_native_load_null(emit_t *emit) {
    emit_native_pre(emit);
    emit_post_push_imm(emit, VTYPE_PYOBJ, 0);
}

STATIC void emit_native_load_fast(emit_t *emit, qstr qst, mp_uint_t id_flags, mp_uint_t local_num) {
    vtype_kind_t vtype = emit->local_vtype[local_num];
    if (vtype == VTYPE_UNBOUND) {
        printf("ViperTypeError: local %s used before type known\n", qstr_str(qst));
    }
    emit_native_pre(emit);
#if N_X64
    if (local_num == 0) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_3);
    } else {
        need_reg_single(emit, REG_RAX, 0);
        asm_x64_mov_local_to_r64(emit->as, local_num - REG_LOCAL_NUM, REG_RAX);
        emit_post_push_reg(emit, vtype, REG_RAX);
    }
#elif N_X86
    if (local_num == 0) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_3);
    } else {
        need_reg_single(emit, REG_EAX, 0);
        asm_x86_mov_local_to_r32(emit->as, local_num - REG_LOCAL_NUM, REG_EAX);
        emit_post_push_reg(emit, vtype, REG_EAX);
    }
#elif N_THUMB
    if (local_num == 0) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_3);
    } else {
        need_reg_single(emit, REG_R0, 0);
        asm_thumb_mov_reg_local(emit->as, REG_R0, local_num - REG_LOCAL_NUM);
        emit_post_push_reg(emit, vtype, REG_R0);
    }
#elif N_ARM
    if (local_num == 0) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_post_push_reg(emit, vtype, REG_LOCAL_3);
    } else {
        need_reg_single(emit, REG_R0, 0);
        asm_arm_mov_reg_local(emit->as, REG_R0, local_num - REG_LOCAL_NUM);
        emit_post_push_reg(emit, vtype, REG_R0);
    }
#else
    #error not implemented
#endif
}

STATIC void emit_native_load_deref(emit_t *emit, qstr qst, mp_uint_t local_num) {
    // not implemented
    // in principle could support this quite easily (ldr r0, [r0, #0]) and then get closed over variables!
    assert(0);
}

STATIC void emit_native_load_name(emit_t *emit, qstr qst) {
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_LOAD_NAME, qst, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_global(emit_t *emit, qstr qst) {
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_LOAD_GLOBAL, qst, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_attr(emit_t *emit, qstr qst) {
    // depends on type of subject:
    //  - integer, function, pointer to integers: error
    //  - pointer to structure: get member, quite easy
    //  - Python object: call mp_load_attr, and needs to be typed to convert result
    vtype_kind_t vtype_base;
    emit_pre_pop_reg(emit, &vtype_base, REG_ARG_1); // arg1 = base
    assert(vtype_base == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_LOAD_ATTR, qst, REG_ARG_2); // arg2 = attribute name
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_method(emit_t *emit, qstr qst) {
    vtype_kind_t vtype_base;
    emit_pre_pop_reg(emit, &vtype_base, REG_ARG_1); // arg1 = base
    assert(vtype_base == VTYPE_PYOBJ);
    emit_get_stack_pointer_to_reg_for_push(emit, REG_ARG_3, 2); // arg3 = dest ptr
    emit_call_with_imm_arg(emit, MP_F_LOAD_METHOD, qst, REG_ARG_2); // arg2 = method name
}

STATIC void emit_native_load_build_class(emit_t *emit) {
    emit_native_pre(emit);
    emit_call(emit, MP_F_LOAD_BUILD_CLASS);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_load_subscr(emit_t *emit) {
    vtype_kind_t vtype_lhs, vtype_rhs;
    emit_pre_pop_reg_reg(emit, &vtype_rhs, REG_ARG_2, &vtype_lhs, REG_ARG_1);
    if (vtype_lhs == VTYPE_PYOBJ && vtype_rhs == VTYPE_PYOBJ) {
        emit_call_with_imm_arg(emit, MP_F_OBJ_SUBSCR, (mp_uint_t)MP_OBJ_SENTINEL, REG_ARG_3);
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    } else {
        printf("ViperTypeError: can't do subscr of types %d and %d\n", vtype_lhs, vtype_rhs);
    }
}

STATIC void emit_native_store_fast(emit_t *emit, qstr qst, mp_uint_t local_num) {
    vtype_kind_t vtype;
#if N_X64
    if (local_num == 0) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_3);
    } else {
        emit_pre_pop_reg(emit, &vtype, REG_RAX);
        asm_x64_mov_r64_to_local(emit->as, REG_RAX, local_num - REG_LOCAL_NUM);
    }
#elif N_X86
    if (local_num == 0) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_3);
    } else {
        emit_pre_pop_reg(emit, &vtype, REG_EAX);
        asm_x86_mov_r32_to_local(emit->as, REG_EAX, local_num - REG_LOCAL_NUM);
    }
#elif N_THUMB
    if (local_num == 0) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_3);
    } else {
        emit_pre_pop_reg(emit, &vtype, REG_R0);
        asm_thumb_mov_local_reg(emit->as, local_num - REG_LOCAL_NUM, REG_R0);
    }
#elif N_ARM
    if (local_num == 0) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_1);
    } else if (local_num == 1) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_2);
    } else if (local_num == 2) {
        emit_pre_pop_reg(emit, &vtype, REG_LOCAL_3);
    } else {
        emit_pre_pop_reg(emit, &vtype, REG_R0);
        asm_arm_mov_local_reg(emit->as, local_num - REG_LOCAL_NUM, REG_R0);
    }
#else
    #error not implemented
#endif

    emit_post(emit);

    // check types
    if (emit->local_vtype[local_num] == VTYPE_UNBOUND) {
        // first time this local is assigned, so give it a type of the object stored in it
        emit->local_vtype[local_num] = vtype;
    } else if (emit->local_vtype[local_num] != vtype) {
        // type of local is not the same as object stored in it
        printf("ViperTypeError: type mismatch, local %s has type %d but source object has type %d\n", qstr_str(qst), emit->local_vtype[local_num], vtype);
    }
}

STATIC void emit_native_store_deref(emit_t *emit, qstr qst, mp_uint_t local_num) {
    // not implemented
    assert(0);
}

STATIC void emit_native_store_name(emit_t *emit, qstr qst) {
    // mp_store_name, but needs conversion of object (maybe have mp_viper_store_name(obj, type))
    vtype_kind_t vtype;
    emit_pre_pop_reg(emit, &vtype, REG_ARG_2);
    assert(vtype == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_STORE_NAME, qst, REG_ARG_1); // arg1 = name
    emit_post(emit);
}

STATIC void emit_native_store_global(emit_t *emit, qstr qst) {
    vtype_kind_t vtype = peek_vtype(emit);
    if (vtype == VTYPE_PYOBJ) {
        emit_pre_pop_reg(emit, &vtype, REG_ARG_2);
    } else {
        emit_pre_pop_reg(emit, &vtype, REG_ARG_1);
        emit_call_with_imm_arg(emit, MP_F_CONVERT_NATIVE_TO_OBJ, vtype, REG_ARG_2); // arg2 = type
        ASM_MOV_REG_TO_REG(emit->as, REG_RET, REG_ARG_2);
    }
    emit_call_with_imm_arg(emit, MP_F_STORE_GLOBAL, qst, REG_ARG_1); // arg1 = name
    emit_post(emit);
}

STATIC void emit_native_store_attr(emit_t *emit, qstr qst) {
    vtype_kind_t vtype_base, vtype_val;
    emit_pre_pop_reg_reg(emit, &vtype_base, REG_ARG_1, &vtype_val, REG_ARG_3); // arg1 = base, arg3 = value
    assert(vtype_base == VTYPE_PYOBJ);
    assert(vtype_val == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_STORE_ATTR, qst, REG_ARG_2); // arg2 = attribute name
    emit_post(emit);
}

STATIC void emit_native_store_subscr(emit_t *emit) {
    // depends on type of subject:
    //  - integer, function, pointer to structure: error
    //  - pointer to integers: store as per array
    //  - Python object: call runtime with converted object or type info
    vtype_kind_t vtype_index, vtype_base, vtype_value;
    emit_pre_pop_reg_reg_reg(emit, &vtype_index, REG_ARG_2, &vtype_base, REG_ARG_1, &vtype_value, REG_ARG_3); // index, base, value to store
    assert(vtype_index == VTYPE_PYOBJ);
    assert(vtype_base == VTYPE_PYOBJ);
    assert(vtype_value == VTYPE_PYOBJ);
    emit_call(emit, MP_F_OBJ_SUBSCR);
}

STATIC void emit_native_delete_fast(emit_t *emit, qstr qst, mp_uint_t local_num) {
    // TODO implement me!
    // could support for Python types, just set to None (so GC can reclaim it)
}

STATIC void emit_native_delete_deref(emit_t *emit, qstr qst, mp_uint_t local_num) {
    // TODO implement me!
}

STATIC void emit_native_delete_name(emit_t *emit, qstr qst) {
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_DELETE_NAME, qst, REG_ARG_1);
    emit_post(emit);
}

STATIC void emit_native_delete_global(emit_t *emit, qstr qst) {
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_DELETE_GLOBAL, qst, REG_ARG_1);
    emit_post(emit);
}

STATIC void emit_native_delete_attr(emit_t *emit, qstr qst) {
    vtype_kind_t vtype_base;
    emit_pre_pop_reg(emit, &vtype_base, REG_ARG_1); // arg1 = base
    assert(vtype_base == VTYPE_PYOBJ);
    emit_call_with_2_imm_args(emit, MP_F_STORE_ATTR, qst, REG_ARG_2, (mp_uint_t)MP_OBJ_NULL, REG_ARG_3); // arg2 = attribute name, arg3 = value (null for delete)
    emit_post(emit);
}

STATIC void emit_native_delete_subscr(emit_t *emit) {
    vtype_kind_t vtype_index, vtype_base;
    emit_pre_pop_reg_reg(emit, &vtype_index, REG_ARG_2, &vtype_base, REG_ARG_1); // index, base
    assert(vtype_index == VTYPE_PYOBJ);
    assert(vtype_base == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_OBJ_SUBSCR, (mp_uint_t)MP_OBJ_NULL, REG_ARG_3);
}

STATIC void emit_native_dup_top(emit_t *emit) {
    vtype_kind_t vtype;
    emit_pre_pop_reg(emit, &vtype, REG_TEMP0);
    emit_post_push_reg_reg(emit, vtype, REG_TEMP0, vtype, REG_TEMP0);
}

STATIC void emit_native_dup_top_two(emit_t *emit) {
    vtype_kind_t vtype0, vtype1;
    emit_pre_pop_reg_reg(emit, &vtype0, REG_TEMP0, &vtype1, REG_TEMP1);
    emit_post_push_reg_reg_reg_reg(emit, vtype1, REG_TEMP1, vtype0, REG_TEMP0, vtype1, REG_TEMP1, vtype0, REG_TEMP0);
}

STATIC void emit_native_pop_top(emit_t *emit) {
    emit_pre_pop_discard(emit);
    emit_post(emit);
}

STATIC void emit_native_rot_two(emit_t *emit) {
    vtype_kind_t vtype0, vtype1;
    emit_pre_pop_reg_reg(emit, &vtype0, REG_TEMP0, &vtype1, REG_TEMP1);
    emit_post_push_reg_reg(emit, vtype0, REG_TEMP0, vtype1, REG_TEMP1);
}

STATIC void emit_native_rot_three(emit_t *emit) {
    vtype_kind_t vtype0, vtype1, vtype2;
    emit_pre_pop_reg_reg_reg(emit, &vtype0, REG_TEMP0, &vtype1, REG_TEMP1, &vtype2, REG_TEMP2);
    emit_post_push_reg_reg_reg(emit, vtype0, REG_TEMP0, vtype2, REG_TEMP2, vtype1, REG_TEMP1);
}

STATIC void emit_native_jump(emit_t *emit, mp_uint_t label) {
    emit_native_pre(emit);
    // need to commit stack because we are jumping elsewhere
    need_stack_settled(emit);
    ASM_JUMP(emit->as, label);
    emit_post(emit);
}

STATIC void emit_native_jump_helper(emit_t *emit, mp_uint_t label, bool pop) {
    vtype_kind_t vtype = peek_vtype(emit);
    if (vtype == VTYPE_BOOL) {
        emit_pre_pop_reg(emit, &vtype, REG_RET);
        if (!pop) {
            adjust_stack(emit, 1);
        }
    } else if (vtype == VTYPE_PYOBJ) {
        emit_pre_pop_reg(emit, &vtype, REG_ARG_1);
        if (!pop) {
            adjust_stack(emit, 1);
        }
        emit_call(emit, MP_F_OBJ_IS_TRUE);
    } else {
        printf("ViperTypeError: expecting a bool or pyobj, got %d\n", vtype);
        assert(0);
    }
    // need to commit stack because we may jump elsewhere
    need_stack_settled(emit);
}

STATIC void emit_native_pop_jump_if_true(emit_t *emit, mp_uint_t label) {
    emit_native_jump_helper(emit, label, true);
    ASM_JUMP_IF_REG_NONZERO(emit->as, REG_RET, label);
    emit_post(emit);
}

STATIC void emit_native_pop_jump_if_false(emit_t *emit, mp_uint_t label) {
    emit_native_jump_helper(emit, label, true);
    ASM_JUMP_IF_REG_ZERO(emit->as, REG_RET, label);
    emit_post(emit);
}

STATIC void emit_native_jump_if_true_or_pop(emit_t *emit, mp_uint_t label) {
    emit_native_jump_helper(emit, label, false);
    ASM_JUMP_IF_REG_NONZERO(emit->as, REG_RET, label);
    adjust_stack(emit, -1);
    emit_post(emit);
}

STATIC void emit_native_jump_if_false_or_pop(emit_t *emit, mp_uint_t label) {
    emit_native_jump_helper(emit, label, false);
    ASM_JUMP_IF_REG_ZERO(emit->as, REG_RET, label);
    adjust_stack(emit, -1);
    emit_post(emit);
}

STATIC void emit_native_break_loop(emit_t *emit, mp_uint_t label, mp_uint_t except_depth) {
    emit_native_jump(emit, label & ~MP_EMIT_BREAK_FROM_FOR); // TODO properly
}

STATIC void emit_native_continue_loop(emit_t *emit, mp_uint_t label, mp_uint_t except_depth) {
    emit_native_jump(emit, label); // TODO properly
}

STATIC void emit_native_setup_with(emit_t *emit, mp_uint_t label) {
    // not supported, or could be with runtime call
    assert(0);
}

STATIC void emit_native_with_cleanup(emit_t *emit) {
    assert(0);
}

STATIC void emit_native_setup_except(emit_t *emit, mp_uint_t label) {
    emit_native_pre(emit);
    // need to commit stack because we may jump elsewhere
    need_stack_settled(emit);
    emit_get_stack_pointer_to_reg_for_push(emit, REG_ARG_1, sizeof(nlr_buf_t) / sizeof(mp_uint_t)); // arg1 = pointer to nlr buf
    emit_call(emit, MP_F_NLR_PUSH);
    ASM_JUMP_IF_REG_NONZERO(emit->as, REG_RET, label);
    emit_post(emit);
}

STATIC void emit_native_setup_finally(emit_t *emit, mp_uint_t label) {
    emit_native_setup_except(emit, label);
}

STATIC void emit_native_end_finally(emit_t *emit) {
    emit_pre_pop_discard(emit);
    emit_post(emit);
}

STATIC void emit_native_get_iter(emit_t *emit) {
    // perhaps the difficult one, as we want to rewrite for loops using native code
    // in cases where we iterate over a Python object, can we use normal runtime calls?

    vtype_kind_t vtype;
    emit_pre_pop_reg(emit, &vtype, REG_ARG_1);
    assert(vtype == VTYPE_PYOBJ);
    emit_call(emit, MP_F_GETITER);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_for_iter(emit_t *emit, mp_uint_t label) {
    emit_native_pre(emit);
    vtype_kind_t vtype;
    emit_access_stack(emit, 1, &vtype, REG_ARG_1);
    assert(vtype == VTYPE_PYOBJ);
    emit_call(emit, MP_F_ITERNEXT);
    ASM_MOV_IMM_TO_REG(emit->as, (mp_uint_t)MP_OBJ_STOP_ITERATION, REG_TEMP1);
    ASM_JUMP_IF_REG_EQ(emit->as, REG_RET, REG_TEMP1, label);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_for_iter_end(emit_t *emit) {
    // adjust stack counter (we get here from for_iter ending, which popped the value for us)
    emit_native_pre(emit);
    adjust_stack(emit, -1);
    emit_post(emit);
}

STATIC void emit_native_pop_block(emit_t *emit) {
    emit_native_pre(emit);
    emit_call(emit, MP_F_NLR_POP);
    adjust_stack(emit, -(mp_int_t)(sizeof(nlr_buf_t) / sizeof(mp_uint_t)));
    emit_post(emit);
}

STATIC void emit_native_pop_except(emit_t *emit) {
    /*
    emit_native_pre(emit);
    emit_call(emit, MP_F_NLR_POP);
    adjust_stack(emit, -(mp_int_t)(sizeof(nlr_buf_t) / sizeof(mp_uint_t)));
    emit_post(emit);
    */
}

STATIC void emit_native_unary_op(emit_t *emit, mp_unary_op_t op) {
    if (op == MP_UNARY_OP_NOT) {
        // we need to synthesise this operation
        assert(0);
    } else {
        vtype_kind_t vtype;
        emit_pre_pop_reg(emit, &vtype, REG_ARG_2);
        assert(vtype == VTYPE_PYOBJ);
        emit_call_with_imm_arg(emit, MP_F_UNARY_OP, op, REG_ARG_1);
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    }
}

STATIC void emit_native_binary_op(emit_t *emit, mp_binary_op_t op) {
    vtype_kind_t vtype_lhs, vtype_rhs;
    emit_pre_pop_reg_reg(emit, &vtype_rhs, REG_ARG_3, &vtype_lhs, REG_ARG_2);
    if (vtype_lhs == VTYPE_INT && vtype_rhs == VTYPE_INT) {
        if (op == MP_BINARY_OP_ADD || op == MP_BINARY_OP_INPLACE_ADD) {
#if N_X64
            asm_x64_add_r64_to_r64(emit->as, REG_ARG_3, REG_ARG_2);
#elif N_X86
            asm_x86_add_r32_to_r32(emit->as, REG_ARG_3, REG_ARG_2);
#elif N_THUMB
            asm_thumb_add_rlo_rlo_rlo(emit->as, REG_ARG_2, REG_ARG_2, REG_ARG_3);
#elif N_ARM
            asm_arm_add_reg(emit->as, REG_ARG_2, REG_ARG_2, REG_ARG_3);
#else
    #error not implemented
#endif
            emit_post_push_reg(emit, VTYPE_INT, REG_ARG_2);
        } else if (op == MP_BINARY_OP_LESS) {
#if N_X64
            asm_x64_xor_r64_to_r64(emit->as, REG_RET, REG_RET);
            asm_x64_cmp_r64_with_r64(emit->as, REG_ARG_3, REG_ARG_2);
            asm_x64_setcc_r8(emit->as, ASM_X64_CC_JL, REG_RET);
#elif N_X86
            asm_x86_xor_r32_to_r32(emit->as, REG_RET, REG_RET);
            asm_x86_cmp_r32_with_r32(emit->as, REG_ARG_3, REG_ARG_2);
            asm_x86_setcc_r8(emit->as, ASM_X86_CC_JL, REG_RET);
#elif N_THUMB
            asm_thumb_cmp_rlo_rlo(emit->as, REG_ARG_2, REG_ARG_3);
            asm_thumb_ite_ge(emit->as);
            asm_thumb_mov_rlo_i8(emit->as, REG_RET, 0); // if r0 >= r1
            asm_thumb_mov_rlo_i8(emit->as, REG_RET, 1); // if r0 < r1
#elif N_ARM
            asm_arm_less_op(emit->as, REG_ARG_2, REG_ARG_3);
#else
    #error not implemented
#endif
            emit_post_push_reg(emit, VTYPE_BOOL, REG_RET);
        } else {
            // TODO other ops not yet implemented
            assert(0);
        }
    } else if (vtype_lhs == VTYPE_PYOBJ && vtype_rhs == VTYPE_PYOBJ) {
        emit_call_with_imm_arg(emit, MP_F_BINARY_OP, op, REG_ARG_1);
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    } else {
        printf("ViperTypeError: can't do binary op between types %d and %d\n", vtype_lhs, vtype_rhs);
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    }
}

STATIC void emit_native_build_tuple(emit_t *emit, mp_uint_t n_args) {
    // for viper: call runtime, with types of args
    //   if wrapped in byte_array, or something, allocates memory and fills it
    emit_native_pre(emit);
    emit_get_stack_pointer_to_reg_for_pop(emit, REG_ARG_2, n_args); // pointer to items
    emit_call_with_imm_arg(emit, MP_F_BUILD_TUPLE, n_args, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET); // new tuple
}

STATIC void emit_native_build_list(emit_t *emit, mp_uint_t n_args) {
    emit_native_pre(emit);
    emit_get_stack_pointer_to_reg_for_pop(emit, REG_ARG_2, n_args); // pointer to items
    emit_call_with_imm_arg(emit, MP_F_BUILD_LIST, n_args, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET); // new list
}

STATIC void emit_native_list_append(emit_t *emit, mp_uint_t list_index) {
    // only used in list comprehension
    vtype_kind_t vtype_list, vtype_item;
    emit_pre_pop_reg(emit, &vtype_item, REG_ARG_2);
    emit_access_stack(emit, list_index, &vtype_list, REG_ARG_1);
    assert(vtype_list == VTYPE_PYOBJ);
    assert(vtype_item == VTYPE_PYOBJ);
    emit_call(emit, MP_F_LIST_APPEND);
    emit_post(emit);
}

STATIC void emit_native_build_map(emit_t *emit, mp_uint_t n_args) {
    emit_native_pre(emit);
    emit_call_with_imm_arg(emit, MP_F_BUILD_MAP, n_args, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET); // new map
}

STATIC void emit_native_store_map(emit_t *emit) {
    vtype_kind_t vtype_key, vtype_value, vtype_map;
    emit_pre_pop_reg_reg_reg(emit, &vtype_key, REG_ARG_2, &vtype_value, REG_ARG_3, &vtype_map, REG_ARG_1); // key, value, map
    assert(vtype_key == VTYPE_PYOBJ);
    assert(vtype_value == VTYPE_PYOBJ);
    assert(vtype_map == VTYPE_PYOBJ);
    emit_call(emit, MP_F_STORE_MAP);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET); // map
}

STATIC void emit_native_map_add(emit_t *emit, mp_uint_t map_index) {
    // only used in list comprehension
    vtype_kind_t vtype_map, vtype_key, vtype_value;
    emit_pre_pop_reg_reg(emit, &vtype_key, REG_ARG_2, &vtype_value, REG_ARG_3);
    emit_access_stack(emit, map_index, &vtype_map, REG_ARG_1);
    assert(vtype_map == VTYPE_PYOBJ);
    assert(vtype_key == VTYPE_PYOBJ);
    assert(vtype_value == VTYPE_PYOBJ);
    emit_call(emit, MP_F_STORE_MAP);
    emit_post(emit);
}

STATIC void emit_native_build_set(emit_t *emit, mp_uint_t n_args) {
    emit_native_pre(emit);
    emit_get_stack_pointer_to_reg_for_pop(emit, REG_ARG_2, n_args); // pointer to items
    emit_call_with_imm_arg(emit, MP_F_BUILD_SET, n_args, REG_ARG_1);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET); // new set
}

STATIC void emit_native_set_add(emit_t *emit, mp_uint_t set_index) {
    // only used in set comprehension
    vtype_kind_t vtype_set, vtype_item;
    emit_pre_pop_reg(emit, &vtype_item, REG_ARG_2);
    emit_access_stack(emit, set_index, &vtype_set, REG_ARG_1);
    assert(vtype_set == VTYPE_PYOBJ);
    assert(vtype_item == VTYPE_PYOBJ);
    emit_call(emit, MP_F_STORE_SET);
    emit_post(emit);
}

STATIC void emit_native_build_slice(emit_t *emit, mp_uint_t n_args) {
    DEBUG_printf("build_slice %d\n", n_args);
    if (n_args == 2) {
        vtype_kind_t vtype_start, vtype_stop;
        emit_pre_pop_reg_reg(emit, &vtype_stop, REG_ARG_2, &vtype_start, REG_ARG_1); // arg1 = start, arg2 = stop
        assert(vtype_start == VTYPE_PYOBJ);
        assert(vtype_stop == VTYPE_PYOBJ);
        emit_call_with_imm_arg(emit, MP_F_NEW_SLICE, (mp_uint_t)mp_const_none, REG_ARG_3); // arg3 = step
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    } else {
        assert(n_args == 3);
        vtype_kind_t vtype_start, vtype_stop, vtype_step;
        emit_pre_pop_reg_reg_reg(emit, &vtype_step, REG_ARG_3, &vtype_stop, REG_ARG_2, &vtype_start, REG_ARG_1); // arg1 = start, arg2 = stop, arg3 = step
        assert(vtype_start == VTYPE_PYOBJ);
        assert(vtype_stop == VTYPE_PYOBJ);
        assert(vtype_step == VTYPE_PYOBJ);
        emit_call(emit, MP_F_NEW_SLICE);
        emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
    }
}

STATIC void emit_native_unpack_sequence(emit_t *emit, mp_uint_t n_args) {
    DEBUG_printf("unpack_sequence %d\n", n_args);
    vtype_kind_t vtype_base;
    emit_pre_pop_reg(emit, &vtype_base, REG_ARG_1); // arg1 = seq
    assert(vtype_base == VTYPE_PYOBJ);
    emit_get_stack_pointer_to_reg_for_push(emit, REG_ARG_3, n_args); // arg3 = dest ptr
    emit_call_with_imm_arg(emit, MP_F_UNPACK_SEQUENCE, n_args, REG_ARG_2); // arg2 = n_args
}

STATIC void emit_native_unpack_ex(emit_t *emit, mp_uint_t n_left, mp_uint_t n_right) {
    DEBUG_printf("unpack_ex %d %d\n", n_left, n_right);
    vtype_kind_t vtype_base;
    emit_pre_pop_reg(emit, &vtype_base, REG_ARG_1); // arg1 = seq
    assert(vtype_base == VTYPE_PYOBJ);
    emit_get_stack_pointer_to_reg_for_push(emit, REG_ARG_3, n_left + n_right + 1); // arg3 = dest ptr
    emit_call_with_imm_arg(emit, MP_F_UNPACK_EX, n_left | (n_right << 8), REG_ARG_2); // arg2 = n_left + n_right
}

STATIC void emit_native_make_function(emit_t *emit, scope_t *scope, mp_uint_t n_pos_defaults, mp_uint_t n_kw_defaults) {
    // call runtime, with type info for args, or don't support dict/default params, or only support Python objects for them
    emit_native_pre(emit);
    if (n_pos_defaults == 0 && n_kw_defaults == 0) {
        emit_call_with_3_imm_args_and_first_aligned(emit, MP_F_MAKE_FUNCTION_FROM_RAW_CODE, (mp_uint_t)scope->raw_code, REG_ARG_1, (mp_uint_t)MP_OBJ_NULL, REG_ARG_2, (mp_uint_t)MP_OBJ_NULL, REG_ARG_3);
    } else {
        vtype_kind_t vtype_def_tuple, vtype_def_dict;
        emit_pre_pop_reg_reg(emit, &vtype_def_dict, REG_ARG_3, &vtype_def_tuple, REG_ARG_2);
        assert(vtype_def_tuple == VTYPE_PYOBJ);
        assert(vtype_def_dict == VTYPE_PYOBJ);
        emit_call_with_imm_arg_aligned(emit, MP_F_MAKE_FUNCTION_FROM_RAW_CODE, (mp_uint_t)scope->raw_code, REG_ARG_1);
    }
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_make_closure(emit_t *emit, scope_t *scope, mp_uint_t n_closed_over, mp_uint_t n_pos_defaults, mp_uint_t n_kw_defaults) {
    assert(0);
}

STATIC void emit_native_call_function(emit_t *emit, mp_uint_t n_positional, mp_uint_t n_keyword, mp_uint_t star_flags) {
    // call special viper runtime routine with type info for args, and wanted type info for return
    assert(!star_flags);

    /* we no longer have these _n specific call_function's
     * they anyway push args into an array
     * and they would take too much room in the native dispatch table
    if (n_positional == 0) {
        vtype_kind_t vtype_fun;
        emit_pre_pop_reg(emit, &vtype_fun, REG_ARG_1); // the function
        assert(vtype_fun == VTYPE_PYOBJ);
        emit_call(emit, MP_F_CALL_FUNCTION_0);
    } else if (n_positional == 1) {
        vtype_kind_t vtype_fun, vtype_arg1;
        emit_pre_pop_reg_reg(emit, &vtype_arg1, REG_ARG_2, &vtype_fun, REG_ARG_1); // the single argument, the function
        assert(vtype_fun == VTYPE_PYOBJ);
        assert(vtype_arg1 == VTYPE_PYOBJ);
        emit_call(emit, MP_F_CALL_FUNCTION_1);
    } else if (n_positional == 2) {
        vtype_kind_t vtype_fun, vtype_arg1, vtype_arg2;
        emit_pre_pop_reg_reg_reg(emit, &vtype_arg2, REG_ARG_3, &vtype_arg1, REG_ARG_2, &vtype_fun, REG_ARG_1); // the second argument, the first argument, the function
        assert(vtype_fun == VTYPE_PYOBJ);
        assert(vtype_arg1 == VTYPE_PYOBJ);
        assert(vtype_arg2 == VTYPE_PYOBJ);
        emit_call(emit, MP_F_CALL_FUNCTION_2);
    } else {
    */

    emit_native_pre(emit);
    if (n_positional != 0 || n_keyword != 0) {
        emit_get_stack_pointer_to_reg_for_pop(emit, REG_ARG_3, n_positional + 2 * n_keyword); // pointer to args
    }
    vtype_kind_t vtype_fun;
    emit_pre_pop_reg(emit, &vtype_fun, REG_ARG_1); // the function
    assert(vtype_fun == VTYPE_PYOBJ);
    emit_call_with_imm_arg(emit, MP_F_NATIVE_CALL_FUNCTION_N_KW, n_positional | (n_keyword << 8), REG_ARG_2);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_call_method(emit_t *emit, mp_uint_t n_positional, mp_uint_t n_keyword, mp_uint_t star_flags) {
    assert(!star_flags);

    /*
    if (n_positional == 0) {
        vtype_kind_t vtype_meth, vtype_self;
        emit_pre_pop_reg_reg(emit, &vtype_self, REG_ARG_2, &vtype_meth, REG_ARG_1); // the self object (or NULL), the method
        assert(vtype_meth == VTYPE_PYOBJ);
        assert(vtype_self == VTYPE_PYOBJ);
        emit_call(emit, MP_F_CALL_METHOD_1);
    } else if (n_positional == 1) {
        vtype_kind_t vtype_meth, vtype_self, vtype_arg1;
        emit_pre_pop_reg_reg_reg(emit, &vtype_arg1, REG_ARG_3, &vtype_self, REG_ARG_2, &vtype_meth, REG_ARG_1); // the first argument, the self object (or NULL), the method
        assert(vtype_meth == VTYPE_PYOBJ);
        assert(vtype_self == VTYPE_PYOBJ);
        assert(vtype_arg1 == VTYPE_PYOBJ);
        emit_call(emit, MP_F_CALL_METHOD_2);
    } else {
    */

    emit_native_pre(emit);
    emit_get_stack_pointer_to_reg_for_pop(emit, REG_ARG_3, 2 + n_positional + 2 * n_keyword); // pointer to items, including meth and self
    emit_call_with_2_imm_args(emit, MP_F_CALL_METHOD_N_KW, n_positional, REG_ARG_1, n_keyword, REG_ARG_2);
    emit_post_push_reg(emit, VTYPE_PYOBJ, REG_RET);
}

STATIC void emit_native_return_value(emit_t *emit) {
    DEBUG_printf("return_value\n");
    vtype_kind_t vtype;
    emit_pre_pop_reg(emit, &vtype, REG_RET);
    if (emit->do_viper_types) {
        if (vtype == VTYPE_PTR_NONE) {
            if (emit->return_vtype == VTYPE_PYOBJ) {
                ASM_MOV_IMM_TO_REG(emit->as, (mp_uint_t)mp_const_none, REG_RET);
            }
        } else if (vtype != emit->return_vtype) {
            printf("ViperTypeError: incompatible return type\n");
        }
    } else {
        assert(vtype == VTYPE_PYOBJ);
    }
    emit->last_emit_was_return_value = true;
    //ASM_BREAK_POINT(emit->as); // to insert a break-point for debugging
    ASM_EXIT(emit->as);
}

STATIC void emit_native_raise_varargs(emit_t *emit, mp_uint_t n_args) {
    assert(n_args == 1);
    vtype_kind_t vtype_exc;
    emit_pre_pop_reg(emit, &vtype_exc, REG_ARG_1); // arg1 = object to raise
    if (vtype_exc != VTYPE_PYOBJ) {
        printf("ViperTypeError: must raise an object\n");
    }
    // TODO probably make this 1 call to the runtime (which could even call convert, native_raise(obj, type))
    emit_call(emit, MP_F_NATIVE_RAISE);
}

STATIC void emit_native_yield_value(emit_t *emit) {
    // not supported (for now)
    assert(0);
}
STATIC void emit_native_yield_from(emit_t *emit) {
    // not supported (for now)
    assert(0);
}

STATIC void emit_native_start_except_handler(emit_t *emit) {
    // This instruction follows an nlr_pop, so the stack counter is back to zero, when really
    // it should be up by a whole nlr_buf_t.  We then want to pop the nlr_buf_t here, but save
    // the first 2 elements, so we can get the thrown value.
    adjust_stack(emit, 2);
    vtype_kind_t vtype_nlr;
    emit_pre_pop_reg(emit, &vtype_nlr, REG_ARG_1); // get the thrown value
    emit_pre_pop_discard(emit); // discard the linked-list pointer in the nlr_buf
    emit_post_push_reg_reg_reg(emit, VTYPE_PYOBJ, REG_ARG_1, VTYPE_PYOBJ, REG_ARG_1, VTYPE_PYOBJ, REG_ARG_1); // push the 3 exception items
}

STATIC void emit_native_end_except_handler(emit_t *emit) {
    adjust_stack(emit, -2);
}

const emit_method_table_t EXPORT_FUN(method_table) = {
    emit_native_set_native_type,
    emit_native_start_pass,
    emit_native_end_pass,
    emit_native_last_emit_was_return_value,
    emit_native_adjust_stack_size,
    emit_native_set_source_line,

    emit_native_load_id,
    emit_native_store_id,
    emit_native_delete_id,

    emit_native_label_assign,
    emit_native_import_name,
    emit_native_import_from,
    emit_native_import_star,
    emit_native_load_const_tok,
    emit_native_load_const_small_int,
    emit_native_load_const_int,
    emit_native_load_const_dec,
    emit_native_load_const_str,
    emit_native_load_null,
    emit_native_load_fast,
    emit_native_load_deref,
    emit_native_load_name,
    emit_native_load_global,
    emit_native_load_attr,
    emit_native_load_method,
    emit_native_load_build_class,
    emit_native_load_subscr,
    emit_native_store_fast,
    emit_native_store_deref,
    emit_native_store_name,
    emit_native_store_global,
    emit_native_store_attr,
    emit_native_store_subscr,
    emit_native_delete_fast,
    emit_native_delete_deref,
    emit_native_delete_name,
    emit_native_delete_global,
    emit_native_delete_attr,
    emit_native_delete_subscr,
    emit_native_dup_top,
    emit_native_dup_top_two,
    emit_native_pop_top,
    emit_native_rot_two,
    emit_native_rot_three,
    emit_native_jump,
    emit_native_pop_jump_if_true,
    emit_native_pop_jump_if_false,
    emit_native_jump_if_true_or_pop,
    emit_native_jump_if_false_or_pop,
    emit_native_break_loop,
    emit_native_continue_loop,
    emit_native_setup_with,
    emit_native_with_cleanup,
    emit_native_setup_except,
    emit_native_setup_finally,
    emit_native_end_finally,
    emit_native_get_iter,
    emit_native_for_iter,
    emit_native_for_iter_end,
    emit_native_pop_block,
    emit_native_pop_except,
    emit_native_unary_op,
    emit_native_binary_op,
    emit_native_build_tuple,
    emit_native_build_list,
    emit_native_list_append,
    emit_native_build_map,
    emit_native_store_map,
    emit_native_map_add,
    emit_native_build_set,
    emit_native_set_add,
    emit_native_build_slice,
    emit_native_unpack_sequence,
    emit_native_unpack_ex,
    emit_native_make_function,
    emit_native_make_closure,
    emit_native_call_function,
    emit_native_call_method,
    emit_native_return_value,
    emit_native_raise_varargs,
    emit_native_yield_value,
    emit_native_yield_from,

    emit_native_start_except_handler,
    emit_native_end_except_handler,
};

#endif
