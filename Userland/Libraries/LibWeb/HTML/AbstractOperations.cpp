/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/VM.h>
#include <LibWeb/Bindings/LocationObject.h>
#include <LibWeb/Bindings/WindowObject.h>
#include <LibWeb/HTML/AbstractOperations.h>
#include <LibWeb/HTML/Scripting/Environments.h>

namespace Web::HTML {

EnvironmentSettingsObject const& current_settings_object(JS::VM& vm)
{
    VERIFY(vm.current_realm());
    VERIFY(vm.current_realm()->custom_data());
    return verify_cast<EnvironmentSettingsObject>(*vm.current_realm()->custom_data());
}

// 7.2.3.1 CrossOriginProperties ( O ), https://html.spec.whatwg.org/multipage/browsers.html#crossoriginproperties-(-o-)
Vector<CrossOriginProperty> const& cross_origin_properties(JS::Object const& window_or_location_object)
{
    // 1. Assert: O is a Location or Window object.
    VERIFY(is<Bindings::LocationObject>(window_or_location_object) || is<Bindings::WindowObject>(window_or_location_object));

    // 2. If O is a Location object, then return « { [[Property]]: "href", [[NeedsGet]]: false, [[NeedsSet]]: true }, { [[Property]]: "replace" } ».
    if (is<Bindings::LocationObject>(window_or_location_object)) {
        static Vector<CrossOriginProperty> s_location_object_cross_origin_properties;
        if (s_location_object_cross_origin_properties.is_empty()) {
            s_location_object_cross_origin_properties.append({ .property = "href", .needs_get = false, .needs_set = true, .original_regular_function = {}, .original_get_function = {}, .original_set_function = Bindings::LocationObject::href_setter });
            s_location_object_cross_origin_properties.append({ .property = "replace", .needs_get = {}, .needs_set = {}, .original_regular_function = Bindings::LocationObject::replace, .original_get_function = {}, .original_set_function = {} });
        }
        return s_location_object_cross_origin_properties;
    }

    // 3. Return « { [[Property]]: "window", [[NeedsGet]]: true, [[NeedsSet]]: false }, { [[Property]]: "self", [[NeedsGet]]: true, [[NeedsSet]]: false },
    //    { [[Property]]: "location", [[NeedsGet]]: true, [[NeedsSet]]: true }, { [[Property]]: "close" }, { [[Property]]: "closed", [[NeedsGet]]: true, [[NeedsSet]]: false },
    //    { [[Property]]: "focus" }, { [[Property]]: "blur" }, { [[Property]]: "frames", [[NeedsGet]]: true, [[NeedsSet]]: false }, { [[Property]]: "length", [[NeedsGet]]: true, [[NeedsSet]]: false },
    //    { [[Property]]: "top", [[NeedsGet]]: true, [[NeedsSet]]: false }, { [[Property]]: "opener", [[NeedsGet]]: true, [[NeedsSet]]: false }, { [[Property]]: "parent", [[NeedsGet]]: true, [[NeedsSet]]: false },
    //    { [[Property]]: "postMessage" } ».
    static Vector<CrossOriginProperty> s_window_object_cross_origin_properties;
    if (s_window_object_cross_origin_properties.is_empty()) {
        s_window_object_cross_origin_properties.append({ .property = "window", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::window_getter, .original_set_function = {} });
        s_window_object_cross_origin_properties.append({ .property = "self", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::window_getter, .original_set_function = {} });
        s_window_object_cross_origin_properties.append({ .property = "location", .needs_get = true, .needs_set = true, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::location_getter, .original_set_function = Bindings::WindowObject::location_setter });

        // FIXME: Set original_regular_function when close is implemented.
        s_window_object_cross_origin_properties.append({ .property = "close", .needs_get = {}, .needs_set = {}, .original_regular_function = {}, .original_get_function = {}, .original_set_function = {} });

        // FIXME: Set original_get_function when closed is implemented.
        s_window_object_cross_origin_properties.append({ .property = "closed", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = {}, .original_set_function = {} });

        // FIXME: Set original_regular_function when focus is implemented.
        s_window_object_cross_origin_properties.append({ .property = "focus", .needs_get = {}, .needs_set = {}, .original_regular_function = {}, .original_get_function = {}, .original_set_function = {} });

        // FIXME: Set original_regular_function when blur is implemented.
        s_window_object_cross_origin_properties.append({ .property = "blur", .needs_get = {}, .needs_set = {}, .original_regular_function = {}, .original_get_function = {}, .original_set_function = {} });

        s_window_object_cross_origin_properties.append({ .property = "frames", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::window_getter, .original_set_function = {} });
        s_window_object_cross_origin_properties.append({ .property = "length", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::length_getter, .original_set_function = {} });
        s_window_object_cross_origin_properties.append({ .property = "top", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::top_getter, .original_set_function = {} });

        // FIXME: Set original_get_function when opener is implemented.
        s_window_object_cross_origin_properties.append({ .property = "opener", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::top_getter, .original_set_function = {} });

        s_window_object_cross_origin_properties.append({ .property = "parent", .needs_get = true, .needs_set = false, .original_regular_function = {}, .original_get_function = Bindings::WindowObject::parent_getter, .original_set_function = {} });

        // FIXME: Set original_regular_function when postMessage is implemented.
        s_window_object_cross_origin_properties.append({ .property = "postMessage", .needs_get = {}, .needs_set = {}, .original_regular_function = {}, .original_get_function = {}, .original_set_function = {} });
    }
    return s_window_object_cross_origin_properties;
}

// 7.2.3.3 IsPlatformObjectSameOrigin ( O ), https://html.spec.whatwg.org/multipage/browsers.html#isplatformobjectsameorigin-(-o-)
bool is_platform_object_same_origin(JS::VM& vm, JS::Object const& platform_object)
{
    // 1. Return true if the current settings object's origin is same origin-domain with O's relevant settings object's origin, and false otherwise.
    // Spec Note: Here the current settings object roughly corresponds to the "caller", because this check occurs before the execution context for the
    //            getter/setter/method in question makes its way onto the JavaScript execution context stack. For example, in the code w.document,
    //            this step is invoked before the document getter is reached as part of the [[Get]] algorithm for the WindowProxy w.
    dbgln("{:p}", platform_object.global_object().associated_realm()->custom_data());
    auto const& relevant_settings_object = verify_cast<EnvironmentSettingsObject>(*platform_object.global_object().associated_realm()->custom_data());
    return current_settings_object(vm).origin().is_same(relevant_settings_object.origin());
}

// 7.2.3.7 CrossOriginOwnPropertyKeys ( O ), https://html.spec.whatwg.org/multipage/browsers.html#crossoriginownpropertykeys-(-o-)
JS::MarkedValueList cross_origin_own_property_keys(JS::VM& vm, JS::Object const& window_or_location_object)
{
    // 1. Let keys be a new empty List.
    JS::MarkedValueList keys { vm.heap() };

    // 2. For each e of ! CrossOriginProperties(O), append e.[[Property]] to keys.
    for (auto& property : cross_origin_properties(window_or_location_object))
        keys.append(JS::js_string(vm, property.property.as_string()));

    // 3. Return the concatenation of keys and « "then", @@toStringTag, @@hasInstance, @@isConcatSpreadable ».
    keys.append(JS::js_string(vm, "then"sv));
    keys.append(vm.well_known_symbol_to_string_tag());
    keys.append(vm.well_known_symbol_has_instance());
    keys.append(vm.well_known_symbol_is_concat_spreadable());
    return keys;
}

}
