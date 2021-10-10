/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Bindings/WindowObject.h>
#include <LibWeb/DOM/Node.h>
#include <LibWeb/DOM/MutationObserver.h>

namespace Web::DOM {

// https://dom.spec.whatwg.org/#dom-mutationobserver-mutationobserver
MutationObserver::MutationObserver(Bindings::WindowObject& window_object, JS::Handle<JS::FunctionObject> callback)
    : m_callback(move(callback))
{
    // 1. Set this’s callback to callback.

    // 2. Append this to this’s relevant agent’s mutation observers.
    // FIXME: This doesn't match the way the spec does it exactly.
    auto* agent_custom_data = verify_cast<Bindings::WebEngineCustomData>(window_object.vm().custom_data());
    agent_custom_data->mutation_observers.append(*this);
}

// https://dom.spec.whatwg.org/#dom-mutationobserver-observe
ExceptionOr<void> MutationObserver::observe(Node& target, MutationObserverInit const& options)
{
    // FIXME: Pls no copy. (Needed to change some options)
    auto options_copy = options;

    // 1. If either options["attributeOldValue"] or options["attributeFilter"] exists, and options["attributes"] does not exist, then set options["attributes"] to true.
    // FIXME: This currently doesn't check for attributeFilter.
    if (options_copy.attribute_old_value.has_value() && !options_copy.attributes.has_value())
        options_copy.attributes = true;

    // 2. If options["characterDataOldValue"] exists and options["characterData"] does not exist, then set options["characterData"] to true.
    if (options_copy.character_data_old_value.has_value() && !options_copy.character_data.has_value())
        options_copy.character_data = true;

    // 3. If none of options["childList"], options["attributes"], and options["characterData"] is true, then throw a TypeError.
    if (!options_copy.child_list && (!options_copy.attributes.has_value() || !options_copy.attributes.value()) && (!options_copy.character_data.has_value() || !options_copy.character_data.value()))
        return SimpleException { SimpleExceptionType::TypeError, "Options must have one of childList, attributes or characterData set to true." };

    // 4. If options["attributeOldValue"] is true and options["attributes"] is false, then throw a TypeError.
    // NOTE: If attributeOldValue is present, attributes will be present because of step 1.
    if (options_copy.attribute_old_value.has_value() && options_copy.attribute_old_value.value() && !options_copy.attributes.value())
        return SimpleException { SimpleExceptionType::TypeError, "attributes must be true if attributeOldValue is true." };

    // FIXME: 5. If options["attributeFilter"] is present and options["attributes"] is false, then throw a TypeError.

    // 6. If options["characterDataOldValue"] is true and options["characterData"] is false, then throw a TypeError.
    // NOTE: If characterDataOldValue is present, characterData will be present because of step 2.
    if (options_copy.character_data_old_value.has_value() && options_copy.character_data_old_value.value() && !options_copy.character_data.value())
        return SimpleException { SimpleExceptionType::TypeError, "characterData must be true if characterDataOldValue is true." };

    // 7. For each registered of target’s registered observer list, if registered’s observer is this:
    bool updated_existing_observer = false;
    for (auto& registered_observer : target.registered_observers_list()) {
        if (registered_observer.observer.ptr() == this) {
            updated_existing_observer = true;

            // 1. For each node of this’s node list, remove all transient registered observers whose source is registered from node’s registered observer list.
            for (auto& node : m_node_list) {
                node.registered_observers_list().remove_all_matching([&registered_observer](RegisteredObserver& observer) {
                    return is<TransientRegisteredObserver>(observer) && verify_cast<TransientRegisteredObserver>(observer).source.ptr() == &registered_observer;
                });
            }

            // 2. Set registered’s options to options.
            registered_observer.options = options_copy;
        }
    }

    // 8. Otherwise:
    if (!updated_existing_observer) {
        // 1. Append a new registered observer whose observer is this and options is options to target’s registered observer list.
        auto new_registered_observer = RegisteredObserver::create(*this, options_copy);
        target.add_registered_observer(new_registered_observer);

        // 2. Append target to this’s node list.
        m_node_list.append(target);
    }

    return {};
}

// https://dom.spec.whatwg.org/#dom-mutationobserver-disconnect
void MutationObserver::disconnect()
{
    // 1. For each node of this’s node list, remove any registered observer from node’s registered observer list for which this is the observer.
    for (auto& node : m_node_list) {
        node.registered_observers_list().remove_all_matching([this](RegisteredObserver& registered_observer) {
            return registered_observer.observer.ptr() == this;
        });
    }

    // 2. Empty this’s record queue.
    m_record_queue.clear();
}

// https://dom.spec.whatwg.org/#dom-mutationobserver-takerecords
NonnullRefPtrVector<MutationRecord> MutationObserver::take_records()
{
    // 1. Let records be a clone of this’s record queue.
    auto records = m_record_queue;

    // 2. Empty this’s record queue.
    m_record_queue.clear();

    // 3. Return records.
    return records;
}

}
