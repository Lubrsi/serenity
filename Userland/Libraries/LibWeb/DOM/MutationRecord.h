/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/RefCounted.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/DOM/NodeList.h>
#include <LibWeb/Bindings/Wrappable.h>

namespace Web::DOM {

// https://dom.spec.whatwg.org/#mutationrecord
class MutationRecord final
    : public RefCounted<MutationRecord>
    , public Bindings::Wrappable {
public:
    using WrapperType = Bindings::MutationRecordWrapper;

    static NonnullRefPtr<MutationRecord> create(String const& type, Node& target, NodeList& added_nodes, NodeList& removed_nodes, Node* previous_sibling, Node* next_sibling, String const& attribute_name, String const& attribute_namespace, String const& old_value)
    {
        return adopt_ref(*new MutationRecord(type, target, added_nodes, removed_nodes, previous_sibling, next_sibling, attribute_name, attribute_namespace, old_value));
    }

    virtual ~MutationRecord() override = default;

    String const& type() const { return m_type; }
    Node const& target() const { return m_target; }
    NodeList const& added_nodes() const { return m_added_nodes; }
    NodeList const& removed_nodes() const { return m_removed_nodes; }
    Node const* previous_sibling() const { return m_previous_sibling; }
    Node const* next_sibling() const { return m_next_sibling; }
    String const& attribute_name() const { return m_attribute_name; }
    String const& attribute_namespace() const { return m_attribute_namespace; }
    String const& old_value() const { return m_old_value; }

private:
    MutationRecord(String const& type, Node& target, NodeList& added_nodes, NodeList& removed_nodes, Node* previous_sibling, Node* next_sibling, String const& attribute_name, String const& attribute_namespace, String const& old_value)
        : m_type(type)
        , m_target(target)
        , m_added_nodes(added_nodes)
        , m_removed_nodes(removed_nodes)
        , m_previous_sibling(previous_sibling)
        , m_next_sibling(next_sibling)
        , m_attribute_name(attribute_name)
        , m_attribute_namespace(attribute_namespace)
        , m_old_value(old_value)
    {
    }

    String m_type;
    NonnullRefPtr<Node> m_target;
    NonnullRefPtr<NodeList> m_added_nodes;
    NonnullRefPtr<NodeList> m_removed_nodes;
    RefPtr<Node> m_previous_sibling;
    RefPtr<Node> m_next_sibling;
    String m_attribute_name;
    String m_attribute_namespace;
    String m_old_value;
};

}
