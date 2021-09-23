/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/Element.h>
#include <LibWeb/HTML/DOMStringMap.h>

namespace Web::HTML {

DOMStringMap::DOMStringMap(DOM::Element& associated_element)
    : m_associated_element(associated_element)
{
}

DOMStringMap::~DOMStringMap()
{
}

Vector<String> DOMStringMap::supported_property_names() const
{
    TODO();
}

// https://html.spec.whatwg.org/multipage/dom.html#dom-domstringmap-nameditem
String DOMStringMap::determine_value_of_named_property(String const&) const
{
    TODO();
}

// https://html.spec.whatwg.org/multipage/dom.html#dom-domstringmap-setitem
void DOMStringMap::set_value_of_new_named_property(String const&, String const&)
{
    TODO();
}

// https://html.spec.whatwg.org/multipage/dom.html#dom-domstringmap-setitem
void DOMStringMap::set_value_of_existing_named_property(String const& name, String const& value)
{
    set_value_of_new_named_property(name, value);
}

bool DOMStringMap::delete_existing_named_property(String const&)
{
    TODO();
}

}
