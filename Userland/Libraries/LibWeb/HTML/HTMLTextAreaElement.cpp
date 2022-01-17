/*
 * Copyright (c) 2020, the SerenityOS developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/HTMLTextAreaElement.h>

namespace Web::HTML {

HTMLTextAreaElement::HTMLTextAreaElement(DOM::Document& document, QualifiedName qualified_name)
    : FormAssociatedElement(document, move(qualified_name))
{
}

HTMLTextAreaElement::~HTMLTextAreaElement()
{
}

// https://html.spec.whatwg.org/multipage/form-elements.html#dom-textarea-defaultvalue
String HTMLTextAreaElement::default_value() const
{
    // The defaultValue attribute's getter must return the element's child text content.
    return child_text_content();
}

// https://html.spec.whatwg.org/multipage/form-elements.html#dom-textarea-defaultvalue
void HTMLTextAreaElement::set_default_value(String const& default_value)
{
    // The defaultValue attribute's setter must string replace all with the given value within this element.
    string_replace_all(default_value);
}

}
