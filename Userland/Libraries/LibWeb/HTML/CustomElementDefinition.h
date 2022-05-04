/*
 * Copyright (c) 2022, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/String.h>
#include <LibWeb/Bindings/CallbackType.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/custom-elements.html#custom-element-definition
struct CustomElementDefinition {
    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-name
    // A name
    //     A valid custom element name
    String name;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-local-name
    // A local name
    //     A local name
    String local_name;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-constructor
    // A Web IDL CustomElementConstructor callback function type value wrapping the custom element constructor
    Bindings::CallbackType constructor;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-observed-attributes
    // A list of observed attributes
    //     A sequence<DOMString>
    Vector<String> observed_attributes;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-lifecycle-callbacks
    // A collection of lifecycle callbacks
    //     A map, whose keys are the strings "connectedCallback", "disconnectedCallback", "adoptedCallback", "attributeChangedCallback",
    //     "formAssociatedCallback", "formDisabledCallback", "formResetCallback", and "formStateRestoreCallback".
    //     The corresponding values are either a Web IDL Function callback function type value, or null.
    //     By default the value of each entry is null.
    HashMap<FlyString, Bindings::CallbackType> lifecycle_callbacks;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-construction-stack
    // A construction stack
    //     A list, initially empty, that is manipulated by the upgrade an element algorithm and the HTML element constructors.
    //     Each entry in the list will be either an element or an already constructed marker.
    Vector<Variant<NonnullRefPtr<HTMLElement>, bool>> constructor_stack;

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-form-associated
    // A form-associated boolean
    //     If this is true, user agent treats elements associated to this custom element definition as form-associated custom elements.
    bool form_associated { false };

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-disable-internals
    // A disable internals boolean
    //     Controls attachInternals().
    bool disable_internals { false };

    // https://html.spec.whatwg.org/multipage/custom-elements.html#concept-custom-element-definition-disable-shadow
    // A disable shadow boolean
    //     Controls attachShadow().
    bool disable_shadow { false };
};



}
