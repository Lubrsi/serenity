/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Runtime/Completion.h>
#include <LibJS/Runtime/Object.h>
#include <LibWeb/Forward.h>

namespace Web {
namespace Bindings {

class LocationObject final : public JS::Object {
    JS_OBJECT(LocationObject, JS::Object);

public:
    explicit LocationObject(JS::GlobalObject&);
    virtual void initialize(JS::GlobalObject&) override;
    virtual ~LocationObject() override;

    virtual JS::ThrowCompletionOr<JS::Object*> internal_get_prototype_of() const override;
    virtual JS::ThrowCompletionOr<bool> internal_set_prototype_of(Object* prototype) override;
    virtual JS::ThrowCompletionOr<bool> internal_is_extensible() const override;
    virtual JS::ThrowCompletionOr<bool> internal_prevent_extensions() override;
    virtual JS::ThrowCompletionOr<Optional<JS::PropertyDescriptor>> internal_get_own_property(JS::PropertyKey const&) const override;
    virtual JS::ThrowCompletionOr<JS::MarkedValueList> internal_own_property_keys() const override;

    // FIXME: There should also be a custom [[GetPrototypeOf]], [[GetOwnProperty]], [[DefineOwnProperty]], [[Get]], [[Set]], [[Delete]] and [[OwnPropertyKeys]],
    //        but we don't have the infrastructure in place to implement them yet.

    // These two functions are public because they need to be accessible from the cross-origin abstract operations.
    // When the href setter or replace function of a cross-origin Location object is accessed, it is shadowed by an anonymous native function that will call the original function declared here.
    JS_DECLARE_NATIVE_FUNCTION(replace);
    JS_DECLARE_NATIVE_FUNCTION(href_setter);

private:
    JS_DECLARE_NATIVE_FUNCTION(reload);

    JS_DECLARE_NATIVE_FUNCTION(href_getter);

    JS_DECLARE_NATIVE_FUNCTION(host_getter);
    JS_DECLARE_NATIVE_FUNCTION(hostname_getter);
    JS_DECLARE_NATIVE_FUNCTION(pathname_getter);
    JS_DECLARE_NATIVE_FUNCTION(hash_getter);
    JS_DECLARE_NATIVE_FUNCTION(search_getter);
    JS_DECLARE_NATIVE_FUNCTION(protocol_getter);
    JS_DECLARE_NATIVE_FUNCTION(port_getter);

    // https://html.spec.whatwg.org/multipage/history.html#defaultproperties
    // Also, every Location object has a [[DefaultProperties]] internal slot representing its own properties at time of its creation.
    JS::MarkedValueList m_default_properties;
};

}
}
