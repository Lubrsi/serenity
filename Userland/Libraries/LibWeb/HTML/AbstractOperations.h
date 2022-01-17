/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Traits.h>
#include <LibJS/Forward.h>
#include <LibWeb/Forward.h>

namespace Web::HTML {

struct CrossOriginProperty {
    JS::PropertyKey property;
    Optional<bool> needs_get;
    Optional<bool> needs_set;
    Function<JS::ThrowCompletionOr<JS::Value>(JS::VM&, JS::GlobalObject&)> original_regular_function;
    Function<JS::ThrowCompletionOr<JS::Value>(JS::VM&, JS::GlobalObject&)> original_get_function;
    Function<JS::ThrowCompletionOr<JS::Value>(JS::VM&, JS::GlobalObject&)> original_set_function;
};

struct CrossOriginKey {
    EnvironmentSettingsObject& current_settings_object;
    EnvironmentSettingsObject& relevant_settings_object;
    JS::PropertyKey property_key;
};

EnvironmentSettingsObject const& current_settings_object(JS::VM& vm);

Vector<CrossOriginProperty> const& cross_origin_properties(JS::Object const& window_or_location_object);

// NOTE: This takes a JS::Object instead of a Wrapper, as this must work with LocationObject and WindowObject, which don't inherit from Wrapper.
[[nodiscard]] bool is_platform_object_same_origin(JS::VM& vm, JS::Object const& platform_object);
JS::MarkedValueList cross_origin_own_property_keys(JS::VM& vm, JS::Object const& window_or_location_object);

}

namespace AK {

template<>
struct Traits<Web::HTML::CrossOriginKey> : public GenericTraits<Web::HTML::CrossOriginKey> {
    static unsigned hash(Web::HTML::CrossOriginKey const& cross_origin_key)
    {
        return pair_int_hash(pair_int_hash(ptr_hash(&cross_origin_key.current_settings_object), ptr_hash(&cross_origin_key.relevant_settings_object)), cross_origin_key.property_key.hash());
    }

    static bool equals(Web::HTML::CrossOriginKey const& a, Web::HTML::CrossOriginKey const& b)
    {
        return &a.current_settings_object == &b.current_settings_object && &a.relevant_settings_object == &b.relevant_settings_object && a.property_key == b.property_key;
    }
};

}
