/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/DOM/Document.h>
#include <LibWeb/DOM/Window.h>
#include <LibWeb/Bindings/WindowObject.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/HTML/Scripting/Environments.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/webappapis.html#environment-settings-object%27s-realm
JS::Realm& EnvironmentSettingsObject::realm()
{
    // An environment settings object's realm execution context's Realm component is the environment settings object's Realm.
    return *realm_execution_context().realm;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-global
JS::GlobalObject& EnvironmentSettingsObject::global_object()
{
    // An environment settings object's Realm then has a [[GlobalObject]] field, which contains the environment settings object's global object.
    return realm().global_object();
}

// https://html.spec.whatwg.org/multipage/webappapis.html#responsible-event-loop
EventLoop& EnvironmentSettingsObject::responsible_event_loop()
{
    // An environment settings object's responsible event loop is its global object's relevant agent's event loop.
    auto& vm = global_object().vm();
    return verify_cast<Bindings::WebEngineCustomData>(vm.custom_data())->event_loop;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#check-if-we-can-run-script
CanRunScript EnvironmentSettingsObject::can_run_script()
{
    // 1. If the global object specified by settings is a Window object whose Document object is not fully active, then return "do not run".
    if (is<Bindings::WindowObject>(global_object()) && !verify_cast<Bindings::WindowObject>(global_object()).impl().associated_document().is_fully_active())
        return CanRunScript::No;

    // FIXME: 2. If scripting is disabled for settings, then return "do not run".

    // 3. Return "run".
    return CanRunScript::Yes;
}

// https://html.spec.whatwg.org/multipage/webappapis.html#prepare-to-run-script
void EnvironmentSettingsObject::prepare_to_run_script()
{
    // 1. Push settings's realm execution context onto the JavaScript execution context stack; it is now the running JavaScript execution context.
    global_object().vm().push_execution_context(realm_execution_context(), global_object());

    // FIXME: 2. Add settings to the currently running task's script evaluation environment settings object set.
}

// https://html.spec.whatwg.org/multipage/webappapis.html#clean-up-after-running-script
void EnvironmentSettingsObject::clean_up_after_running_script()
{
    auto& vm = global_object().vm();

    // 1. Assert: settings's realm execution context is the running JavaScript execution context.
    VERIFY(&realm_execution_context() == &vm.running_execution_context());

    // 2. Remove settings's realm execution context from the JavaScript execution context stack.
    vm.pop_execution_context();

    // 3. If the JavaScript execution context stack is now empty, perform a microtask checkpoint. (If this runs scripts, these algorithms will be invoked reentrantly.)
    if (vm.execution_context_stack().is_empty())
        HTML::main_thread_event_loop().perform_a_microtask_checkpoint();
}

}
