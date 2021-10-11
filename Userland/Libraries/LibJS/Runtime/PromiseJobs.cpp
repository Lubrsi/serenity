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
PromiseJob PromiseReactionJob::create(GlobalObject& global_object, PromiseReaction& reaction, Value argument)
{
    // 1. Let job be a new Job Abstract Closure with no parameters that captures reaction and argument and performs the following steps when called:
    //    See PromiseReactionJob::call for "the following steps".
    auto* job = global_object.heap().allocate<PromiseReactionJob>(global_object, reaction, argument, *global_object.function_prototype());

    // 2. Let handlerRealm be null.
    Realm* handler_realm { nullptr };

    // 3. If reaction.[[Handler]] is not empty, then
    auto& handler = reaction.handler();
    if (handler.has_value()) {
        // a. Let getHandlerRealmResult be GetFunctionRealm(reaction.[[Handler]].[[Callback]]).
        auto get_handler_realm_result = get_function_realm(global_object, *reaction.handler()->callback);

        // b. If getHandlerRealmResult is a normal completion, set handlerRealm to getHandlerRealmResult.[[Value]].
        if (!get_handler_realm_result.is_throw_completion()) {
            handler_realm = get_handler_realm_result.release_value();
        }

        // c. Else, set handlerRealm to the current Realm Record.
        else {
            // FIXME: Is clearing the exception here correct? If so, does it also require a stop_unwind?
            global_object.vm().clear_exception();
            handler_realm = global_object.vm().current_realm();
        }

        // d. NOTE: handlerRealm is never null unless the handler is undefined. When the handler is a revoked Proxy and no ECMAScript code runs, handlerRealm is used to create error objects.
        VERIFY(handler_realm);
    }

    // 4. Return the Record { [[Job]]: job, [[Realm]]: handlerRealm }.
    return { *job, handler_realm };
}

PromiseReactionJob::PromiseReactionJob(PromiseReaction& reaction, Value argument, Object& prototype)
    : NativeFunction(prototype)
    , m_reaction(reaction)
    , m_argument(argument)
{
}

// 27.2.2.1 NewPromiseReactionJob ( reaction, argument ), https://tc39.es/ecma262/#sec-newpromisereactionjob
Value PromiseReactionJob::call()
{
    auto& vm = this->vm();
    auto& promise_capability = m_reaction.capability();
    auto type = m_reaction.type();
    auto& handler = m_reaction.handler();
    Value handler_result;
    if (!handler.has_value()) {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Handler is empty", this);
        switch (type) {
        case PromiseReaction::Type::Fulfill:
            dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Reaction type is Type::Fulfill, setting handler result to {}", this, m_argument);
            handler_result = m_argument;
            break;
        case PromiseReaction::Type::Reject:
            dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Reaction type is Type::Reject, throwing exception with argument {}", this, m_argument);
            vm.throw_exception(global_object(), m_argument);
            // handler_result is set to exception value further below
            break;
        }
    } else {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Calling handler callback {} @ {} with argument {}", this, handler.value().callback->class_name(), handler.value().callback, m_argument);
        MarkedValueList arguments(vm.heap());
        arguments.append(m_argument);
        handler_result = vm.host_call_job_callback(handler.value(), js_undefined(), move(arguments));
    }

    if (!promise_capability.has_value()) {
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Reaction has no PromiseCapability, returning empty value", this);
        return {};
    }

    if (vm.exception()) {
        handler_result = vm.exception()->value();
        vm.clear_exception();
        vm.stop_unwind();
        auto* reject_function = promise_capability.value().reject;
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Calling PromiseCapability's reject function @ {}", this, reject_function);
        return TRY_OR_DISCARD(vm.call(*reject_function, js_undefined(), handler_result));
    } else {
        auto* resolve_function = promise_capability.value().resolve;
        dbgln_if(PROMISE_DEBUG, "[PromiseReactionJob @ {}]: Calling PromiseCapability's resolve function @ {}", this, resolve_function);
        return TRY_OR_DISCARD(vm.call(*resolve_function, js_undefined(), handler_result));
    }
}

void PromiseReactionJob::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(&m_reaction);
    visitor.visit(m_argument);
}

// 27.2.2.2 NewPromiseResolveThenableJob ( promiseToResolve, thenable, then ), https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
PromiseJob PromiseResolveThenableJob::create(GlobalObject& global_object, Promise& promise_to_resolve, Value thenable, JobCallback then)
{
    // 1. Let job be a new Job Abstract Closure with no parameters that captures promiseToResolve, thenable, and then and performs the following steps when called:
    //    See PromiseResolveThenableJob::call() for "the following steps".
    auto* job = global_object.heap().allocate<PromiseResolveThenableJob>(global_object, promise_to_resolve, thenable, move(then), *global_object.function_prototype());

    // 2. Let getThenRealmResult be GetFunctionRealm(then.[[Callback]]).
    Realm* then_realm { nullptr };
    auto get_then_realm_result = get_function_realm(global_object, *then.callback);

    // 3. If getThenRealmResult is a normal completion, let thenRealm be getThenRealmResult.[[Value]].
    if (!get_then_realm_result.is_throw_completion()) {
        then_realm = get_then_realm_result.release_value();
    }

    // 4. Else, let thenRealm be the current Realm Record.
    else {
        // FIXME: Is clearing the exception here correct? If so, does it also require a stop_unwind?
        global_object.vm().clear_exception();
        then_realm = global_object.vm().current_realm();
    }

    // 5. NOTE: thenRealm is never null. When then.[[Callback]] is a revoked Proxy and no code runs, thenRealm is used to create error objects.
    VERIFY(then_realm);

    // 6. Return the Record { [[Job]]: job, [[Realm]]: thenRealm }.
    return { *job, then_realm };
}

PromiseResolveThenableJob::PromiseResolveThenableJob(Promise& promise_to_resolve, Value thenable, JobCallback then, Object& prototype)
    : NativeFunction(prototype)
    , m_promise_to_resolve(promise_to_resolve)
    , m_thenable(thenable)
    , m_then(move(then))
{
}

// 27.2.2.2 NewPromiseResolveThenableJob ( promiseToResolve, thenable, then ), https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
Value PromiseResolveThenableJob::call()
{
    auto& vm = this->vm();
    auto [resolve_function, reject_function] = m_promise_to_resolve.create_resolving_functions();
    dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob @ {}]: Calling then job callback for thenable {}", this, &m_thenable);
    MarkedValueList arguments(vm.heap());
    arguments.append(Value(&resolve_function));
    arguments.append(Value(&reject_function));
    auto then_call_result = vm.host_call_job_callback(m_then, m_thenable, move(arguments));
    if (vm.exception()) {
        auto error = vm.exception()->value();
        vm.clear_exception();
        vm.stop_unwind();
        dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob @ {}]: An exception was thrown, returning error {}", this, error);
        return error;
    }
    dbgln_if(PROMISE_DEBUG, "[PromiseResolveThenableJob @ {}]: Returning then call result {}", this, then_call_result);
    return then_call_result;
}

void PromiseResolveThenableJob::visit_edges(Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(&m_promise_to_resolve);
    visitor.visit(m_thenable);
    visitor.visit(m_then.callback);
}

}
