/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefPtr.h>
#include <LibWeb/Forward.h>

namespace Web::DOM {

// https://dom.spec.whatwg.org/#slotable
template<typename NodeType>
class Slottable {
public:
    // https://dom.spec.whatwg.org/#dom-slotable-assignedslot
    RefPtr<HTML::HTMLSlotElement> assigned_slot()
    {
        
    }
};

}
