/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2023, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtr.h>
#include <AK/StringView.h>
#include <AK/Types.h>
#include <LibJS/SourceCode.h>

namespace JS {

struct Position {
    size_t line { 0 };
    size_t column { 0 };
    size_t offset { 0 };
};

struct SourceRange {
    [[nodiscard]] bool contains(Position const& position) const { return position.offset <= end.offset && position.offset >= start.offset; }

    NonnullRefPtr<SourceCode const> code;
    Position start;
    Position end;

    DeprecatedString filename() const;
};

struct ShrinkWrappedSourceRange {
    ShrinkWrappedSourceRange(u32 start_offset, u32 end_offset, NonnullRefPtr<SourceCode const> source_code)
        : start_offset(start_offset)
        , end_offset(end_offset)
        , source_code(source_code)
    {
    }

    u32 start_offset { 0 };
    u32 end_offset { 0 };
    NonnullRefPtr<SourceCode const> source_code;

    [[nodiscard]] SourceRange to_source_range() const
    {
        return source_code->range_from_offsets(start_offset, end_offset);
    }
};

}
