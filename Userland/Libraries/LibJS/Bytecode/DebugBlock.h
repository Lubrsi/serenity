/*
 * Copyright (c) 2023, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/SourceRange.h>

namespace JS::Bytecode {

struct DebugBlock {
    static NonnullOwnPtr<DebugBlock> create(u8 const* start_of_instructions, ShrinkWrappedSourceRange const& shrink_wrapped_source_range)
    {
        return adopt_own(*new DebugBlock(start_of_instructions, shrink_wrapped_source_range));
    }

    ~DebugBlock() = default;

    [[nodiscard]] bool contains(u8 const* instruction_pointer) const {
        return start_of_instructions <= instruction_pointer && instruction_pointer <= end_of_instructions;
    }

    u8 const* start_of_instructions { nullptr };
    u8 const* end_of_instructions { nullptr };
    ShrinkWrappedSourceRange shrink_wrapped_source_range;

private:
    DebugBlock(u8 const* start_of_instructions, ShrinkWrappedSourceRange const& shrink_wrapped_source_range)
        : start_of_instructions(start_of_instructions)
        , shrink_wrapped_source_range(shrink_wrapped_source_range)
    {
    }
};

struct DebugBlockChain {
    DebugBlockChain const* previous { nullptr };
    DebugBlock& debug_block;
};

}
