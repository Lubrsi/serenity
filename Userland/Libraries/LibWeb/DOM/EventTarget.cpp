/*
 * Copyright (c) 2020-2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StringBuilder.h>
#include <LibJS/Interpreter.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/ECMAScriptFunctionObject.h>
#include <LibWeb/Bindings/ScriptExecutionContext.h>
#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Event.h>
#include <LibWeb/DOM/EventDispatcher.h>
#include <LibWeb/DOM/EventListener.h>
#include <LibWeb/DOM/EventTarget.h>
#include <LibWeb/DOM/Window.h>
#include <LibWeb/HTML/HTMLBodyElement.h>
#include <LibWeb/HTML/HTMLFrameSetElement.h>
#include <LibWeb/HTML/EventHandler.h>

namespace Web::DOM {

EventTarget::EventTarget(Bindings::ScriptExecutionContext& script_execution_context)
    : m_script_execution_context(&script_execution_context)
{
}

EventTarget::~EventTarget()
{
}

// https://dom.spec.whatwg.org/#add-an-event-listener
void EventTarget::add_event_listener(const FlyString& event_name, RefPtr<EventListener> listener)
{
    if (listener.is_null())
        return;
    auto existing_listener = m_listeners.first_matching([&](auto& entry) {
        return entry.listener->type() == event_name && &entry.listener->callback() == &listener->callback() && entry.listener->capture() == listener->capture();
    });
    if (existing_listener.has_value())
        return;
    listener->set_type(event_name);
    m_listeners.append({ event_name, listener.release_nonnull() });
}

// https://dom.spec.whatwg.org/#remove-an-event-listener
void EventTarget::remove_event_listener(const FlyString& event_name, RefPtr<EventListener> listener)
{
    if (listener.is_null())
        return;
    m_listeners.remove_first_matching([&](auto& entry) {
        auto matches = entry.event_name == event_name && &entry.listener->callback() == &listener->callback() && entry.listener->capture() == listener->capture();
        if (matches)
            entry.listener->set_removed(true);
        return matches;
    });
}

void EventTarget::remove_from_event_listener_list(NonnullRefPtr<EventListener> listener)
{
    m_listeners.remove_first_matching([&](auto& entry) {
        return entry.listener->type() == listener->type() && &entry.listener->callback() == &listener->callback() && entry.listener->capture() == listener->capture();
    });
}

// https://dom.spec.whatwg.org/#dom-eventtarget-dispatchevent
ExceptionOr<bool> EventTarget::dispatch_event_binding(NonnullRefPtr<Event> event)
{
    if (event->dispatched())
        return DOM::InvalidStateError::create("The event is already being dispatched.");

    if (!event->initialized())
        return DOM::InvalidStateError::create("Cannot dispatch an uninitialized event.");

    event->set_is_trusted(false);

    return dispatch_event(event);
}

// https://html.spec.whatwg.org/multipage/webappapis.html#determining-the-target-of-an-event-handler
static EventTarget* determine_target_of_event_handler(EventTarget& event_target, FlyString const& name)
{
    // To determine the target of an event handler, given an EventTarget object eventTarget on which the event handler is exposed,
    // and an event handler name name, the following steps are taken:

    // 1. If eventTarget is not a body element or a frameset element, then return eventTarget.
    if (!is<HTML::HTMLBodyElement>(event_target) && !is<HTML::HTMLFrameSetElement>(event_target))
        return &event_target;

    auto& event_target_element = static_cast<HTML::HTMLElement&>(event_target);

    // FIXME: 2. If name is not the name of an attribute member of the WindowEventHandlers interface mixin and the Window-reflecting
    //           body element event handler set does not contain name, then return eventTarget.
    (void)name;

    // 3. If eventTarget's node document is not an active document, then return null.
    if (!event_target_element.document().is_active())
        return nullptr;

    // Return eventTarget's node document's relevant global object.
    return &event_target_element.document().window();
}

HTML::EventHandler EventTarget::event_handler_attribute(FlyString const& name)
{
    auto target = determine_target_of_event_handler(*this, name);
    if (!target)
        return {};

    for (auto& listener : target->listeners()) {
        if (listener.event_name == name && listener.listener->is_attribute()) {
            return HTML::EventHandler { listener.listener->callback().callback };
        }
    }
    return {};
}

// https://html.spec.whatwg.org/multipage/webappapis.html#getting-the-current-value-of-the-event-handler
Bindings::CallbackType* EventTarget::get_current_value_of_event_handler(FlyString const& name)
{
    // 1. Let handlerMap be eventTarget's event handler map. (NOTE: Not necessary)

    // 2. Let eventHandler be handlerMap[name].
    auto event_handler = m_event_handler_map.get(name);

    // Optimization: The spec creates all the event handlers exposed on an object up front and has the initial value of the handler set to null.
    //               If the event handler hasn't been set, null would be returned in step 4.
    //               However, this would be very allocation heavy. For example, each DOM::Element includes GlobalEventHandlers, which defines 60+(!) event handler attributes.
    //               Plus, the vast majority of these allocations would be likely wasted, as I imagine web content will only use a handful of these attributes on certain elements, if any at all.
    //               Thus, we treat the event handler not being in the event handler map as being equivalent to an event handler with an initial null value.
    if (!event_handler.has_value())
        return nullptr;

    // 3. If eventHandler's value is an internal raw uncompiled handler, then:
    if (event_handler->value.has<String>()) {
        // 1. If eventTarget is an element, then let element be eventTarget, and document be element's node document.
        //    Otherwise, eventTarget is a Window object, let element be null, and document be eventTarget's associated Document.
        RefPtr<Element> element;
        RefPtr<Document> document;

        if (is<Element>(this)) {
            auto* element_event_target = verify_cast<Element>(this);
            element = element_event_target;
            document = element_event_target->document();
        } else {
            VERIFY(is<Window>(this));
            auto* window_event_target = verify_cast<Window>(this);
            document = window_event_target->associated_document();
        }

        VERIFY(document);

        // 2. If scripting is disabled for document, then return null.
        if (document->is_scripting_disabled())
            return nullptr;

        // 3. Let body be the uncompiled script body in eventHandler's value.
        auto& body = event_handler->value.get<String>();

        // FIXME: 4. Let location be the location where the script body originated, as given by eventHandler's value.

        // FIXME: 5. If element is not null and element has a form owner, let form owner be that form owner. Otherwise, let form owner be null.

        // 6. Let settings object be the relevant settings object of document.
        auto& settings_object = document->relevant_settings_object();

        // NOTE: ECMAScriptFunctionObject::create expects a parsed body as input, so we must do the spec's sourceText steps here.
        StringBuilder builder;

        // sourceText
        if (is<Window>(this)) {
            //  -> If name is onerror and eventTarget is a Window object
            //      The string formed by concatenating "function ", name, "(event, source, lineno, colno, error) {", U+000A LF, body, U+000A LF, and "}".
            builder.appendff("function {}(event, source, lineno, colno, error) {{\n{}\n}}", name, body);
        } else {
            //  -> Otherwise
            //      The string formed by concatenating "function ", name, "(event) {", U+000A LF, body, U+000A LF, and "}".
            builder.appendff("function {}(event) {{\n{}\n}}", name, body);
        }

        auto parser = JS::Parser(JS::Lexer(builder.string_view()));
        auto program = parser.parse_function_node<JS::FunctionExpression>();

        // 7. If body is not parsable as FunctionBody or if parsing detects an early error, then follow these substeps:
        if (parser.has_errors()) {
            // 1. Set eventHandler's value to null.
            //    Note: This does not deactivate the event handler, which additionally removes the event handler's listener (if present).
            m_event_handler_map.remove(name);

            // FIXME: 2. Report the error for the appropriate script and with the appropriate position (line number and column number) given by location, using settings object's global object.
            //           If the error is still not handled after this, then the error may be reported to a developer console.

            // 3. Return null.
            return nullptr;
        }

        // 8. Push settings object's realm execution context onto the JavaScript execution context stack; it is now the running JavaScript execution context.
        auto& global_object = settings_object.global_object();
        global_object.vm().push_execution_context(settings_object.realm_execution_context(), global_object);

        // 9. Let function be the result of calling OrdinaryFunctionCreate, with arguments:
        // functionPrototype
        //  %Function.prototype% (This is enforced by using JS::ECMAScriptFunctionObject)

        // sourceText was handled above.

        // ParameterList
        //  If name is onerror and eventTarget is a Window object
        //    Let the function have five arguments, named event, source, lineno, colno, and error.
        //  Otherwise
        //    Let the function have a single argument called event.
        // (This was handled above for us by the parser using sourceText)

        // body
        //  The result of parsing body above. (This is given by program->body())

        // thisMode
        //  non-lexical-this (For JS::ECMAScriptFunctionObject, this means passing is_arrow_function as false)
        constexpr bool is_arrow_function = false;

        // scope
        //  1. Let realm be settings object's Realm.
        auto& realm = settings_object.realm();

        //  2. Let scope be realm.[[GlobalEnv]].
        auto& scope = realm.global_environment();

        // These can't currently be implemented as new_object_environment expects a JS::Object, which none of these are.
        //  FIXME: 3. If eventHandler is an element's event handler, then set scope to NewObjectEnvironment(document, true, scope).
        //  FIXME: 4. If form owner is not null, then set scope to NewObjectEnvironment(form owner, true, scope).
        //  FIXME: 5. If element is not null, then set scope to NewObjectEnvironment(element, true, scope).

        //  6. Return scope. (NOTE: Not necessary)

        // FIXME: Work out might_need_arguments_object here. Let's assume it might for now.
        auto* function = JS::ECMAScriptFunctionObject::create(global_object, name, program->body(), program->parameters(), program->function_length(), &scope, JS::FunctionKind::Regular, program->is_strict_mode(), true, is_arrow_function);
        VERIFY(function);

        // 10. Remove settings object's realm execution context from the JavaScript execution context stack.
        VERIFY(global_object.vm().execution_context_stack().last() == &settings_object.realm_execution_context());
        global_object.vm().pop_execution_context();

        // 11. Set function.[[ScriptOrModule]] to null.
        function->set_script_or_module({});

        // 12. Set eventHandler's value to the result of creating a Web IDL EventHandler callback function object whose object reference is function and whose callback context is settings object.
        event_handler->value = Bindings::CallbackType { JS::make_handle(static_cast<JS::Object*>(function)), settings_object };
    }

    // 4. Return eventHandler's value.
    VERIFY(event_handler->value.has<Bindings::CallbackType>());
    return event_handler->value.get_pointer<Bindings::CallbackType>();
}

// https://html.spec.whatwg.org/multipage/webappapis.html#event-handler-attributes:event-handler-idl-attributes-3
void EventTarget::set_event_handler_attribute(FlyString const& name, HTML::EventHandler value)
{
    // 1. Let eventTarget be the result of determining the target of an event handler given this object and name.
    auto target = determine_target_of_event_handler(*this, name);

    // 2. If eventTarget is null, then return.
    if (!target)
        return;

    TODO();

    // The EventListener's callback context can be arbitrary; it does not impact the steps of the event handler processing algorithm. [DOM]
    // https://html.spec.whatwg.org/multipage/webappapis.html#activate-an-event-handler
    // FIXME: Since this function is not spec compliant, the callback context probably matters here and is probably wrong.
    RefPtr<DOM::EventListener> listener;
    if (!value.callback.is_null()) {
        Bindings::CallbackType callback(move(value.callback), HTML::incumbent_settings_object());
        listener = adopt_ref(*new DOM::EventListener(move(callback), true));
    } else {
        StringBuilder builder;
        builder.appendff("function {}(event) {{\n{}\n}}", name, value.string);
        auto parser = JS::Parser(JS::Lexer(builder.string_view()));
        auto program = parser.parse_function_node<JS::FunctionExpression>();
        if (parser.has_errors()) {
            dbgln("Failed to parse script in event handler attribute '{}'", name);
            return;
        }
        auto* function = JS::ECMAScriptFunctionObject::create(target->script_execution_context()->realm().global_object(), name, program->body(), program->parameters(), program->function_length(), nullptr, JS::FunctionKind::Regular, false, false);
        VERIFY(function);
        Bindings::CallbackType callback(move(value.callback), HTML::incumbent_settings_object());
        listener = adopt_ref(*new DOM::EventListener(move(callback), true));
    }
    if (listener) {
        for (auto& registered_listener : target->listeners()) {
            if (registered_listener.event_name == name && registered_listener.listener->is_attribute()) {
                target->remove_event_listener(name, registered_listener.listener);
                break;
            }
        }
        target->add_event_listener(name, listener.release_nonnull());
    }
}

bool EventTarget::dispatch_event(NonnullRefPtr<Event> event)
{
    return EventDispatcher::dispatch(*this, move(event));
}

}
