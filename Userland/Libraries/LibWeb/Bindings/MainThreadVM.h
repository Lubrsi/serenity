/*
 * Copyright (c) 2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtrVector.h>
#include <LibJS/Forward.h>
#include <LibJS/Runtime/VM.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/DOM/MutationObserver.h>

namespace Web::Bindings {

struct WebEngineCustomData final : public JS::VM::CustomData {
    virtual ~WebEngineCustomData() override { }

    HTML::EventLoop event_loop;

    // FIXME: These should only be on similar-origin window agents, but we don't currently differentiate agent types.

    // https://dom.spec.whatwg.org/#mutation-observer-compound-microtask-queued-flag
    bool mutation_observer_microtask_queued { false };

    // https://dom.spec.whatwg.org/#mutation-observer-list
    // FIXME: This should be a set.
    NonnullRefPtrVector<DOM::MutationObserver> mutation_observers;
};

JS::VM& main_thread_vm();

}
