/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/String.h>
#include <LibJS/Heap/Handle.h>
#include <LibJS/Runtime/FunctionObject.h>
#include <LibWeb/Bindings/CallbackType.h>

namespace Web::HTML {

struct EventHandler : public Bindings::CallbackType {
    EventHandler(JS::Handle<JS::Object> callback)
        : callback(move(c))
    {
    }

    String string;
    JS::Handle<JS::FunctionObject> callback;
};

}
