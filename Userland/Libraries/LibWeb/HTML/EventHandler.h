/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <AK/Variant.h>
#include <LibJS/Heap/Handle.h>
#include <LibJS/Runtime/FunctionObject.h>

namespace Web::HTML {

struct EventHandler {
    EventHandler(String s)
        : value(move(s))
    {
    }

    EventHandler(JS::Handle<JS::FunctionObject> c)
        : value(move(c))
    {
    }

    // Either uncompiled source code or a function object e.g. from setting an event handler attribute to a function.
    // https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-value
    // NOTE: This does not contain Empty as part of the optimization of not allocating all event handler attributes up front.
    // FIXME: The string should actually be an "internal raw uncompiled handler" struct. This struct is just the uncompiled source code plus a source location for reporting parse errors.
    //        https://html.spec.whatwg.org/multipage/webappapis.html#internal-raw-uncompiled-handler
    Variant<String, JS::Handle<JS::FunctionObject>> value;
};

}
