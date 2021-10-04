/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibJS/Forward.h>
#include <LibWeb/HTML/Scripting/Environments.h>

namespace Web::Bindings::IDL {

bool is_an_array_index(JS::GlobalObject&, JS::PropertyName const&);

// https://heycam.github.io/webidl/#call-a-user-objects-operation
template<typename... Args>
JS::Completion call_user_object_operation(JS::Object& object, JS::Realm& realm, String const& operation_name, Optional<JS::Value> this_argument, Args&&... args)
{
    // 1. Let completion be an uninitialized variable.
    JS::Completion completion;

    // 2. If thisArg was not given, let thisArg be undefined.
    if (!this_argument.has_value())
        this_argument = JS::js_undefined();

    // 3. Let O be the ECMAScript object corresponding to value. (This is enforced by the type of `object`)

    // FIXME: 4. Let realm be O’s associated Realm.
    //        We cannot get the realm from JS::Object, so we currently have to take the realm as an object.

    // 5. Let relevant settings be realm’s settings object.
    auto& relevant_settings = verify_cast<HTML::EnvironmentSettingsObject>(*realm.custom_data());

    // FIXME: 6. Let stored settings be value’s callback context.

    // 7. Prepare to run script with relevant settings.
    relevant_settings.prepare_to_run_script();

    // FIXME: 8. Prepare to run a callback with stored settings.

    // 9. Let X be O.
    auto const* actual_function_object = &object;

    // 10. If ! IsCallable(O) is false, then:
    if (!object.is_function()) {
        // 1. Let getResult be Get(O, opName).
        auto get_result = object.get(operation_name);

        // 2. If getResult is an abrupt completion, set completion to getResult and jump to the step labeled return.
        if (get_result.is_throw_completion()) {
            completion = get_result.throw_completion();
            goto return_cleanup;
        }

        // 4. If ! IsCallable(X) is false, then set completion to a new Completion{[[Type]]: throw, [[Value]]: a newly created TypeError object, [[Target]]: empty}, and jump to the step labeled return.
        if (!get_result.value().is_function()) {
            completion = JS::Completion { JS::Completion::Type::Throw, JS::TypeError::create(realm.global_object(), String::formatted(JS::ErrorType::NotAFunction.message(), get_result.value().to_string_without_side_effects())), {} };
            goto return_cleanup;
        }

        // 3. Set X to getResult.[[Value]].
        // NOTE: This is done out of order because `actual_function_object` is of type JS::Object and we cannot assign to it until we know for sure getResult.[[Value]] is a JS::Object.
        actual_function_object = &get_result.value().as_object();
    }

    // FIXME: 11. Let esArgs be the result of converting args to an ECMAScript arguments list. If this throws an exception, set completion to the completion value representing the thrown exception and jump to the step labeled return.
    //        For simplicity, we currently make the caller do this. However, this means we can't throw exceptions at this point like the spec wants us to.

    // 12. Let callResult be Call(X, thisArg, esArgs).
    auto call_result = realm.vm().call(*actual_function_object, this_argument.value(), forward<Args>(args)...);

    // 13. If callResult is an abrupt completion, set completion to callResult and jump to the step labeled return.
    if (call_result.is_throw_completion()) {
        completion = call_result.throw_completion();
        goto return_cleanup;
    }

    // FIXME: 14. Set completion to the result of converting callResult.[[Value]] to an IDL value of the same type as the operation’s return type.
    //            (This doesn't wrap the value)
    completion = call_result.value();

    // 15. Return: at this point completion will be set to an ECMAScript completion value.
return_cleanup:
    // FIXME: 1. Clean up after running a callback with stored settings.

    // 2. Clean up after running script with relevant settings.
    relevant_settings.clean_up_after_running_script();

    // 3. If completion is a normal completion, return completion.
    if (completion.type() == JS::Completion::Type::Normal)
        return completion;

    // 4. If completion is an abrupt completion and the operation has a return type that is not a promise type, return completion.
    // FIXME: This does not handle promises and thus always returns.
    return completion;
}

}
