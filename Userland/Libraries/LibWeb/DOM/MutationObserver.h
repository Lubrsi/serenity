/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/NonnullRefPtrVector.h>
#include <AK/RefCounted.h>
#include <LibJS/Heap/Handle.h>
#include <LibWeb/DOM/ExceptionOr.h>
#include <LibWeb/Bindings/Wrappable.h>

namespace Web::DOM {

// https://dom.spec.whatwg.org/#dictdef-mutationobserverinit
struct MutationObserverInit {
    bool child_list { false };
    Optional<bool> attributes;
    Optional<bool> character_data;
    bool subtree { false };
    Optional<bool> attribute_old_value;
    Optional<bool> character_data_old_value;
    // FIXME: sequence<DOMString> attributeFilter;
};

// https://dom.spec.whatwg.org/#mutationobserver
class MutationObserver final
    : public RefCounted<MutationObserver>
    , public Bindings::Wrappable {
public:
    using WrapperType = Bindings::MutationObserverWrapper;

    static NonnullRefPtr<MutationObserver> create_with_global_object(Bindings::WindowObject& window_object, JS::Handle<JS::FunctionObject> callback)
    {
        return adopt_ref(*new MutationObserver(window_object, move(callback)));
    }

    virtual ~MutationObserver() override = default;

    ExceptionOr<void> observe(Node& target, MutationObserverInit const& options = {});
    void disconnect();
    NonnullRefPtrVector<MutationRecord> take_records();

private:
    MutationObserver(Bindings::WindowObject& window_object, JS::Handle<JS::FunctionObject> callback);

    // https://dom.spec.whatwg.org/#concept-mo-callback
    JS::Handle<JS::FunctionObject> m_callback;

    // https://dom.spec.whatwg.org/#mutationobserver-node-list
    // FIXME: This may need to hold both WeakPtrs and NonnullRefPtrs, as RegisteredObserver, which contains a MutationObserver, must only hold a weak reference to the node it is stored in.
    Vector<WeakPtr<Node>> m_node_list;

    // https://dom.spec.whatwg.org/#concept-mo-queue
    NonnullRefPtrVector<MutationRecord> m_record_queue;
};

// https://dom.spec.whatwg.org/#registered-observer
struct RegisteredObserver : public RefCounted<RegisteredObserver> {
    static NonnullRefPtr<RegisteredObserver> create(MutationObserver& observer, MutationObserverInit& options)
    {
        return adopt_ref(*new RegisteredObserver(observer, options));
    }

    RegisteredObserver(MutationObserver& observer, MutationObserverInit& options)
        : observer(observer)
        , options(options)
    {
    }

    virtual ~RegisteredObserver() = default;

    NonnullRefPtr<MutationObserver> observer;
    MutationObserverInit options;
};

// https://dom.spec.whatwg.org/#transient-registered-observer
struct TransientRegisteredObserver final : public RegisteredObserver {
    static NonnullRefPtr<TransientRegisteredObserver> create(MutationObserver& observer, MutationObserverInit& options, RegisteredObserver& source)
    {
        return adopt_ref(*new TransientRegisteredObserver(observer, options, source));
    }

    TransientRegisteredObserver(MutationObserver& observer, MutationObserverInit& options, RegisteredObserver& source)
        : RegisteredObserver(observer, options)
        , source(source)
    {
    }

    virtual ~TransientRegisteredObserver() override = default;

    NonnullRefPtr<RegisteredObserver> source;
};

}
