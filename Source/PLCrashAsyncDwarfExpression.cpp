/*
 * Copyright (c) 2013 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

extern "C" {
    #include "PLCrashAsyncDwarfExpression.h"
    #include "PLCrashAsyncDwarfPrimitives.h"
    #include <inttypes.h>
}

#include "dwarf_stack.h"

/**
 * @internal
 * @ingroup plcrash_async_dwarf
 * @{
 */

/**
 * Read a value of type and size @a T from @a *p, advancing @a p the past the read value,
 * and verifying that the read will not overrun the address @a maxpos.
 *
 * @param p The pointer from which the value will be read. This pointer will be advanced by sizeof(T).
 * @param maxpos The maximum address from which a value may be read.
 * @param result The destination to which the result will be written.
 *
 * @return Returns true on success, or false if the read would exceed the boundry specified by @a maxpos.
 */
template <typename T> static inline bool dw_expr_read_impl (void **p, void *maxpos, T *result) {
    if ((uint8_t *)maxpos - (uint8_t *)*p < sizeof(T)) {
        return false;
    }
    
    *result = *((T *)*p);
    *((uint8_t **)p) += sizeof(T);
    return true;
}

/**
 * Evaluate the expression opcodes starting at address expression evaluation imlementation.
 */

/**
 * Evaluate a DWARF expression, as defined in the DWARF 3 Specification, Section 2.5. This
 * internal implementation is templated to support 32-bit and 64-bit evaluation.
 *
 * @param mobj The memory object from which the expression opcodes will be read.
 * @param byteoder The byte order of the data referenced by @a mobj.
 * @param start The task-relative address within @a mobj at which the opcodes will be fetched.
 * @param end The task-relative terminating address for the opcode evaluation.
 * @param result[out] On success, the evaluation result. As per DWARF 3 section 2.5.1, this will be
 * the top-most element on the evaluation stack. If the stack is empty, an error will be returned
 * and no value will be written to this parameter.
 *
 * @return Returns PLCRASH_ESUCCESS on success, or an appropriate plcrash_error_t values
 * on failure. If an invalid opcode is detected, PLCRASH_ENOTSUP will be returned. If the stack
 * is empty upon termination of evaluation, PLCRASH_EINVAL will be returned.
 *
 * @return Returns PLCRASH_ESUCCESS on success, or an appropriate plcrash_error_t values
 * on failure.
 */
template <typename machine_ptr> static plcrash_error_t plcrash_async_dwarf_eval_expression_int (plcrash_async_mobject_t *mobj,
                                                                                                const plcrash_async_byteorder_t *byteorder,
                                                                                                pl_vm_address_t start,
                                                                                                pl_vm_address_t end,
                                                                                                machine_ptr *result)
{
    // TODO: Review the use of an up-to-800 byte stack allocation; we may want to replace this with
    // use of the new async-safe allocator.
    plcrash::dwarf_stack<machine_ptr, 100> stack;

    /* Map in the full instruction range */
    void *instr = plcrash_async_mobject_remap_address(mobj, start, 0, end-start);
    void *instr_max = (uint8_t *)instr + (end - start);

    if (instr == NULL) {
        PLCF_DEBUG("Could not map the DWARF instructions; range falls outside mapped pages");
        return PLCRASH_EINVAL;
    }
    
    /* A position-advancing read macro that uses GCC/clang's compound statement value extension, returning PLCRASH_EINVAL
     * if the read extends beyond the mapped range. */
#define dw_expr_read(_type) ({ \
    _type v; \
    if (!dw_expr_read_impl<_type>(&p, instr_max, &v)) { \
        PLCF_DEBUG("Read of size %zu exceeds mapped range", sizeof(v)); \
        return PLCRASH_EINVAL; \
    } \
    v; \
})
    
    /* A position-advancing uleb128 read macro that uses GCC/clang's compound statement value extension, returning an error
     * if the read fails. */
#define dw_expr_read_uleb128() ({ \
    plcrash_error_t err; \
    uint64_t v; \
    pl_vm_size_t lebsize; \
    pl_vm_off_t offset = ((uint8_t *)p - (uint8_t *)instr); \
    \
    if ((err = plcrash_async_dwarf_read_uleb128(mobj, start, offset, &v, &lebsize)) != PLCRASH_ESUCCESS) { \
        PLCF_DEBUG("Read of ULEB128 value failed"); \
        return err; \
    } \
    \
    p = (uint8_t *)p + lebsize; \
    v; \
})

    /* A position-advancing sleb128 read macro that uses GCC/clang's compound statement value extension, returning an error
     * if the read fails. */
#define dw_expr_read_sleb128() ({ \
    plcrash_error_t err; \
    int64_t v; \
    pl_vm_size_t lebsize; \
    pl_vm_off_t offset = ((uint8_t *)p - (uint8_t *)instr); \
    \
    if ((err = plcrash_async_dwarf_read_sleb128(mobj, start, offset, &v, &lebsize)) != PLCRASH_ESUCCESS) { \
        PLCF_DEBUG("Read of SLEB128 value failed"); \
        return err; \
    } \
    \
    p = (uint8_t *)p + lebsize; \
    v; \
})
    
    /* A push macro that handles reporting of stack overflow errors */
#define dw_expr_push(v) if (!stack.push(v)) { \
    PLCF_DEBUG("Hit stack limit; cannot push further values"); \
    return PLCRASH_EINTERNAL; \
}

    void *p = instr;
    while (p < instr_max) {
        uint8_t opcode = dw_expr_read(uint8_t);
        switch (opcode) {
            case DW_OP_lit0:
            case DW_OP_lit1:
            case DW_OP_lit2:
            case DW_OP_lit3:
            case DW_OP_lit4:
            case DW_OP_lit5:
            case DW_OP_lit6:
            case DW_OP_lit7:
            case DW_OP_lit8:
            case DW_OP_lit9:
            case DW_OP_lit10:
            case DW_OP_lit11:
            case DW_OP_lit12:
            case DW_OP_lit13:
            case DW_OP_lit14:
            case DW_OP_lit15:
            case DW_OP_lit16:
            case DW_OP_lit17:
            case DW_OP_lit18:
            case DW_OP_lit19:
            case DW_OP_lit20:
            case DW_OP_lit21:
            case DW_OP_lit22:
            case DW_OP_lit23:
            case DW_OP_lit24:
            case DW_OP_lit25:
            case DW_OP_lit26:
            case DW_OP_lit27:
            case DW_OP_lit28:
            case DW_OP_lit29:
            case DW_OP_lit30:
            case DW_OP_lit31:
                dw_expr_push(opcode-DW_OP_lit0);
                break;
                
            case DW_OP_const1u:
                dw_expr_push(dw_expr_read(uint8_t));
                break;

            case DW_OP_const1s:
                dw_expr_push(dw_expr_read(int8_t));
                break;
                
            case DW_OP_const2u:
                dw_expr_push(byteorder->swap16(dw_expr_read(uint16_t)));
                break;
                
            case DW_OP_const2s:
                dw_expr_push((int16_t) byteorder->swap16(dw_expr_read(int16_t)));
                break;
                
            case DW_OP_const4u:
                dw_expr_push(byteorder->swap32(dw_expr_read(uint32_t)));
                break;
                
            case DW_OP_const4s:
                dw_expr_push((int32_t) byteorder->swap32(dw_expr_read(int32_t)));
                break;
                
            case DW_OP_const8u:
                dw_expr_push(byteorder->swap64(dw_expr_read(uint64_t)));
                break;
                
            case DW_OP_const8s:
                dw_expr_push((int64_t) byteorder->swap64(dw_expr_read(int64_t)));
                break;
                
            case DW_OP_constu:
                dw_expr_push(dw_expr_read_uleb128());
                break;
                
            case DW_OP_consts:
                dw_expr_push(dw_expr_read_sleb128());
                break;

            case DW_OP_nop: // no-op
                break;

            default:
                PLCF_DEBUG("Unsupported opcode 0x%" PRIx8, opcode);
                return PLCRASH_ENOTSUP;
        }
    }

    /* Provide the result */
    if (!stack.pop(result)) {
        PLCF_DEBUG("Expression did not provide a result value.");
        return PLCRASH_EINVAL;
    }

#undef dw_expr_read
#undef dw_expr_push
    return PLCRASH_ESUCCESS;
}

/**
 * Evaluate a DWARF expression, as defined in the DWARF 3 Specification, Section 2.5.
 *
 * @param mobj The memory object from which the expression opcodes will be read.
 * @param byteoder The byte order of the data referenced by @a mobj.
 * @param address_size The native address size of the target architecture. Currently, only 4 and 8
 * byte address widths are supported.
 * @param address The task-relative address within @a mobj at which the opcodes will be fetched.
 * @param offset An offset to be applied to @a address.
 * @param length The total length of the opcodes readable at @a address + @a offset.
 * @param result[out] On success, the evaluation result. As per DWARF 3 section 2.5.1, this will be
 * the top-most element on the evaluation stack. If the stack is empty, an error will be returned
 * and no value will be written to this parameter.
 *
 * @return Returns PLCRASH_ESUCCESS on success, or an appropriate plcrash_error_t values
 * on failure. If an invalid opcode is detected, PLCRASH_ENOTSUP will be returned. If the stack
 * is empty upon termination of evaluation, PLCRASH_EINVAL will be returned.
 */
plcrash_error_t plcrash_async_dwarf_eval_expression (plcrash_async_mobject_t *mobj,
                                                     uint8_t address_size,
                                                     const plcrash_async_byteorder_t *byteorder,
                                                     pl_vm_address_t address,
                                                     pl_vm_off_t offset,
                                                     pl_vm_size_t length,
                                                     uint64_t *result)
{
    plcrash_error_t err;
    pl_vm_address_t start;
    pl_vm_address_t end;

    /* Calculate the terminating address */
    if (!plcrash_async_address_apply_offset(address, offset, &start)) {
        PLCF_DEBUG("Offset overflows base address");
        return PLCRASH_EINVAL;
    }
    
    if (length > PL_VM_OFF_MAX || !plcrash_async_address_apply_offset(start, length, &end)) {
        PLCF_DEBUG("Length overflows base address");
        return PLCRASH_EINVAL;
    }
    
    /* Call the appropriate implementation for the target's native pointer size */
    switch (address_size) {
        case 4: {
            uint32_t v;
            if ((err = plcrash_async_dwarf_eval_expression_int<uint32_t>(mobj, byteorder, start, end, &v)) == PLCRASH_ESUCCESS)
                *result = v;
            return err;
        }
        case 8: {
            uint64_t v;
            if ((err = plcrash_async_dwarf_eval_expression_int<uint64_t>(mobj, byteorder, start, end, &v)) == PLCRASH_ESUCCESS)
                *result = v;
            return err;
        }
        default:
            PLCF_DEBUG("Unsupported address size of %" PRIu8, address_size);
            return PLCRASH_EINVAL;
    }
}

/**
 * @}
 */