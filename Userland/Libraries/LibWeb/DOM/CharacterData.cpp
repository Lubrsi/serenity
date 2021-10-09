/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/CharacterData.h>
#include <LibWeb/DOM/Document.h>

namespace Web::DOM {

CharacterData::CharacterData(Document& document, NodeType type, const String& data)
    : Node(document, type)
    , m_data(data)
{
}

CharacterData::~CharacterData()
{
}

void CharacterData::set_data(String data)
{
    if (m_data == data)
        return;
    m_data = move(data);
    set_needs_style_update(true);
}

// https://dom.spec.whatwg.org/#dom-node-nodevalue
String CharacterData::node_value() const
{
    return m_data;
}

// https://dom.spec.whatwg.org/#dom-node-nodevalue
void CharacterData::set_node_value(String const& value)
{
    // FIXME: Replace data with node this, offset 0, count this’s length, and data the given value.
    m_data = value;
}

}
