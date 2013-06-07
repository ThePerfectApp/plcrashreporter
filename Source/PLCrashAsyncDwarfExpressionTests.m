/*
 * Author: Landon Fuller <landonf@plausible.coop>
 *
 * Copyright (c) 2012-2013 Plausible Labs Cooperative, Inc.
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

#import "PLCrashTestCase.h"

#include "PLCrashAsyncDwarfExpression.h"

@interface PLCrashAsyncDwarfExpressionTests : PLCrashTestCase {
}
@end

/**
 * Test DWARF expression evaluation.
 */
@implementation PLCrashAsyncDwarfExpressionTests

/* Perform evaluation of the given opcodes, expecting a result of type @a type,
 * with an expected value of @a expected. The data is interpreted as big endian,
 * as to simplify formulating multi-byte test values in the opcode stream */
#define PERFORM_EVAL_TEST(opcodes, type, expected) do { \
    plcrash_async_mobject_t mobj; \
    plcrash_error_t err; \
    uint64_t result;\
\
    STAssertEquals(PLCRASH_ESUCCESS, plcrash_async_mobject_init(&mobj, mach_task_self(), &opcodes, sizeof(opcodes), true), @"Failed to initialize mobj"); \
\
    err = plcrash_async_dwarf_eval_expression(&mobj, 8, plcrash_async_byteorder_big_endian(), &opcodes, 0, sizeof(opcodes), &result); \
    STAssertEquals(err, PLCRASH_ESUCCESS, @"Evaluation failed"); \
    STAssertEquals((type)result, (type)expected, @"Incorrect result"); \
} while(0)

/**
 * Test evaluation of the DW_OP_litN opcodes.
 */
- (void) testLitN {
    for (uint64_t i = 0; i < (DW_OP_lit31 - DW_OP_lit0); i++) {
        uint8_t opcodes[] = {
            DW_OP_lit0 + i // The opcodes are defined in monotonically increasing order.
        };
        
        PERFORM_EVAL_TEST(opcodes, uint64_t, i);
    }
}

/**
 * Test evaluation of the DW_OP_const1u opcode
 */
- (void) testConst1u {
    uint8_t opcodes[] = { DW_OP_const1u, 0xFF };
    PERFORM_EVAL_TEST(opcodes, uint64_t, 0xFF);
}

/**
 * Test evaluation of the DW_OP_const1s opcode
 */
- (void) testConst1s {
    uint8_t opcodes[] = { DW_OP_const1s, 0x80 };
    PERFORM_EVAL_TEST(opcodes, int64_t, INT8_MIN);
}

/**
 * Test evaluation of the DW_OP_const2u opcode
 */
- (void) testConst2u {
    uint8_t opcodes[] = { DW_OP_const2u, 0xFF, 0xFA};
    PERFORM_EVAL_TEST(opcodes, uint64_t, 0xFFFA);
}

/**
 * Test evaluation of the DW_OP_const2s opcode
 */
- (void) testConst2s {
    uint8_t opcodes[] = { DW_OP_const2s, 0x80, 0x00 };
    PERFORM_EVAL_TEST(opcodes, int64_t, INT16_MIN);
}

/**
 * Test evaluation of the DW_OP_const4u opcode
 */
- (void) testConst4u {
    uint8_t opcodes[] = { DW_OP_const4u, 0xFF, 0xFF, 0xFF, 0xFA};
    PERFORM_EVAL_TEST(opcodes, uint64_t, 0xFFFFFFFA);
}

/**
 * Test evaluation of the DW_OP_const4s opcode
 */
- (void) testConst4s {
    uint8_t opcodes[] = { DW_OP_const4s, 0x80, 0x00, 0x00, 0x00 };
    PERFORM_EVAL_TEST(opcodes, int64_t, INT32_MIN);
}

/**
 * Test evaluation of the DW_OP_const8u opcode
 */
- (void) testConst8u {
    uint8_t opcodes[] = { DW_OP_const8u, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFA};
    PERFORM_EVAL_TEST(opcodes, uint64_t, 0xFFFFFFFFFFFFFFFA);
}

/**
 * Test evaluation of the DW_OP_const8s opcode
 */
- (void) testConst8s {
    uint8_t opcodes[] = { DW_OP_const8s, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    PERFORM_EVAL_TEST(opcodes, int64_t, INT64_MIN);
}


/**
 * Test evaluation of the DW_OP_constu (ULEB128 constant) opcode
 */
- (void) testConstu {
    uint8_t opcodes[] = { DW_OP_constu, 0+0x80, 0x1 };
    PERFORM_EVAL_TEST(opcodes, uint64_t, 128);
}

/**
 * Test evaluation of the DW_OP_consts (SLEB128 constant) opcode
 */
- (void) testConsts {
    uint8_t opcodes[] = { DW_OP_consts, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x7f};
    PERFORM_EVAL_TEST(opcodes, int64_t, INT64_MIN);
}

/** Test basic evaluation of a NOP. */
- (void) testNop {
    uint8_t opcodes[] = {
        DW_OP_nop,
        DW_OP_lit31 // at least one result must be available
    };
    
    PERFORM_EVAL_TEST(opcodes, uint64_t, 31);
}

/**
 * Test handling of an empty result.
 */
- (void) testEmptyStackResult {
    plcrash_async_mobject_t mobj;
    plcrash_error_t err;
    uint64_t result;
    uint8_t opcodes[] = {
        DW_OP_nop // push nothing onto the stack
    };
    
    STAssertEquals(PLCRASH_ESUCCESS, plcrash_async_mobject_init(&mobj, mach_task_self(), &opcodes, sizeof(opcodes), true), @"Failed to initialize mobj");
    
    err = plcrash_async_dwarf_eval_expression(&mobj, 8, &plcrash_async_byteorder_direct, &opcodes, 0, sizeof(opcodes), &result);
    STAssertEquals(err, PLCRASH_EINVAL, @"Evaluation of a no-result expression should have failed with EINVAL");
}

/**
 * Test invalid opcode handling
 */
- (void) testInvalidOpcode {
    plcrash_async_mobject_t mobj;
    plcrash_error_t err;
    uint64_t result;
    uint8_t opcodes[] = {
        // Arbitrarily selected bad instruction value.
        // This -could- be allocated to an opcode in the future, but
        // then our test will fail and we can pick another one.
        0x0 
    };
    
    STAssertEquals(PLCRASH_ESUCCESS, plcrash_async_mobject_init(&mobj, mach_task_self(), &opcodes, sizeof(opcodes), true), @"Failed to initialize mobj");
    
    err = plcrash_async_dwarf_eval_expression(&mobj, 8, &plcrash_async_byteorder_direct, &opcodes, 0, sizeof(opcodes), &result);
    STAssertEquals(err, PLCRASH_ENOTSUP, @"Evaluation of a bad opcode should have failed with ENOTSUP");
}


@end