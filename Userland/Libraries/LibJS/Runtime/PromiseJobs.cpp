/*
 * Copyright (c) 2021, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#define PROMISE_DEBUG 1

#include <AK/Debug.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/JobCallback.h>
#include <LibJS/Runtime/Promise.h>
#include <LibJS/Runtime/PromiseJobs.h>
#include <LibJS/Runtime/PromiseReaction.h>
#include <LibJS/Runtime/AbstractOperations.h>

namespace JS {

// 27.2.2.1 NewPromiseReactionJob ( reaction, argument ), https://tc39.es/ecma262/#sec-newpromisereactionjob
static Value run_reaction_job(GlobalObject& global_object, PromiseReaction& reaction, Value argument)
{
    auto& vm = global_object.vm();
    auto& promise_capability = reaction.capability();
    auto type = reaction.type();
    auto& handler = reaction.handler();
    Value handler_result;
    if (!handler.has_value()) {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Handler is empty");
        switch (type) {
        case PromiseReaction::Type::Fulfill:
            dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Reaction type is Type::Fulfill, setting handler result to {}", argument);
            handler_result = argument;
            break;
        case PromiseReaction::Type::Reject:
            dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Reaction type is Type::Reject, throwing exception with argument {}", argument);
            vm.throw_exception(global_object, argument);
            // handler_result is set to exception value further below
            break;
        }
    } else {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Calling handler callback {} @ {} with argument {}", handler.value().callback.cell()->class_name(), handler.value().callback.cell(), argument);
        MarkedValueList arguments(vm.heap());
        arguments.append(argument);
        handler_result = vm.host_call_job_callback(handler.value(), js_undefined(), move(arguments));
    }

    if (!promise_capability.has_value()) {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Reaction has no PromiseCapability, returning empty value");
        return {};
    }

    if (vm.exception()) {
        handler_result = vm.exception()->value();
        vm.clear_exception();
        vm.stop_unwind();
        auto* reject_function = promise_capability.value().reject;
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Calling PromiseCapability's reject function @ {}", reject_function);
        return TRY_OR_DISCARD(vm.call(*reject_function, js_undefined(), handler_result));
    } else {
        auto* resolve_function = promise_capability.value().resolve;
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob]: Calling PromiseCapability's resolve function @ {}", resolve_function);
        return TRY_OR_DISCARD(vm.call(*resolve_function, js_undefined(), handler_result));
    }
}

// 27.2.2.1 NewPromiseReactionJob ( reaction, argument ), https://tc39.es/ecma262/#sec-newpromisereactionjob
PromiseJob create_promise_reaction_job(GlobalObject& global_object, PromiseReaction& reaction, Value argument)
{
    // 1. Let job be a new Job Abstract Closure with no parameters that captures reaction and argument and performs the following steps when called:
    //    See run_reaction_job for "the following steps".
    auto job = [global_object = JS::make_handle(&global_object), reaction = JS::make_handle(&reaction), argument = JS::make_handle(argument)]() mutable {
        return run_reaction_job(*global_object.cell(), *reaction.cell(), argument.value());
    };

    // 2. Let handlerRealm be null.
    Realm* handler_realm { nullptr };

    // 3. If reaction.[[Handler]] is not empty, then
    auto& handler = reaction.handler();
    if (handler.has_value()) {
        // a. Let getHandlerRealmResult be GetFunctionRealm(reaction.[[Handler]].[[Callback]]).
        auto get_handler_realm_result = get_function_realm(global_object, *handler->callback.cell());

        // b. If getHandlerRealmResult is a normal completion, set handlerRealm to getHandlerRealmResult.[[Value]].
        if (!get_handler_realm_result.is_throw_completion()) {
            handler_realm = get_handler_realm_result.release_value();
        } else {
            // c. Else, set handlerRealm to the current Realm Record.
            global_object.vm().clear_exception();
            global_object.vm().stop_unwind();
            handler_realm = global_object.vm().current_realm();
        }

        // d. NOTE: handlerRealm is never null unless the handler is undefined. When the handler is a revoked Proxy and no ECMAScript code runs, handlerRealm is used to create error objects.
    }

    // 4. Return the Record { [[Job]]: job, [[Realm]]: handlerRealm }.
    return { move(job), handler_realm };
}

// 27.2.2.2 NewPromiseResolveThenableJob ( promiseToResolve, thenable, then ), https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
static Value run_resolve_thenable_job(GlobalObject& global_object, Promise& promise_to_resolve, Value thenable, JobCallback& then)
{
    auto& vm = global_object.vm();
    auto [resolve_function, reject_function] = promise_to_resolve.create_resolving_functions();
    dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob]: Calling then job callback for thenable {}", &thenable);
    MarkedValueList arguments(vm.heap());
    arguments.append(Value(&resolve_function));
    arguments.append(Value(&reject_function));
    auto then_call_result = vm.host_call_job_callback(then, thenable, move(arguments));
    if (vm.exception()) {
        auto error = vm.exception()->value();
        vm.clear_exception();
        vm.stop_unwind();
        dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob]: An exception was thrown, returning error {}", error);
        return error;
    }
    dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob]: Returning then call result {}", then_call_result);
    return then_call_result;
}

// 27.2.2.2 NewPromiseResolveThenableJob ( promiseToResolve, thenable, then ), https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
PromiseJob create_promise_resolve_thenable_job(GlobalObject& global_object, Promise& promise_to_resolve, Value thenable, JobCallback then)
{
    // 2. Let getThenRealmResult be GetFunctionRealm(then.[[Callback]]).
    Realm* then_realm { nullptr };
    auto get_then_realm_result = get_function_realm(global_object, *then.callback.cell());

    // 3. If getThenRealmResult is a normal completion, let thenRealm be getThenRealmResult.[[Value]].
    if (!get_then_realm_result.is_throw_completion()) {
        then_realm = get_then_realm_result.release_value();
    } else {
        // 4. Else, let thenRealm be the current Realm Record.
        global_object.vm().clear_exception();
        global_object.vm().stop_unwind();
        then_realm = global_object.vm().current_realm();
    }

    // 5. NOTE: thenRealm is never null. When then.[[Callback]] is a revoked Proxy and no code runs, thenRealm is used to create error objects.
    VERIFY(then_realm);

    // 1. Let job be a new Job Abstract Closure with no parameters that captures promiseToResolve, thenable, and then and performs the following steps when called:
    //    See PromiseResolveThenableJob::call() for "the following steps".
    //    NOTE: This is done out of order, since `then` is moved into the lambda and `then` would be invalid if it was done at the start.
    auto job = [global_object = JS::make_handle(&global_object), promise_to_resolve = JS::make_handle(&promise_to_resolve), thenable = JS::make_handle(thenable), then = move(then)]() mutable {
        return run_resolve_thenable_job(*global_object.cell(), *promise_to_resolve.cell(), thenable.value(), then);
    };

    // 6. Return the Record { [[Job]]: job, [[Realm]]: thenRealm }.
    return { move(job), then_realm };
}

}
