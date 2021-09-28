/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/URL.h>
#include <LibJS/Runtime/ExecutionContext.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Realm.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/HTML/EventLoop/EventLoop.h>
#include <LibWeb/Origin.h>
#include <LibWeb/Page/BrowsingContext.h>

namespace Web::HTML {

// https://html.spec.whatwg.org/multipage/webappapis.html#environment
struct Environment {
    // FIXME: An id https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-id

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-creation-url
    AK::URL creation_url;

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-top-level-creation-url
    AK::URL top_level_creation_url;

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-top-level-origin
    Origin top_level_origin;

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-target-browsing-context
    RefPtr<BrowsingContext> target_browsing_context;

    // FIXME: An active service worker https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-active-service-worker

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-execution-ready-flag
    bool execution_ready { false };
};

enum class CanUseCrossOriginIsolatedAPIs {
    No,
    Yes,
};

// https://html.spec.whatwg.org/multipage/webappapis.html#environment-settings-object
struct EnvironmentSettingsObject : public Environment {
    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-environment-target-browsing-context
    virtual JS::ExecutionContext& realm_execution_context() = 0;

    // FIXME: A module map https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-module-map

    // https://html.spec.whatwg.org/multipage/webappapis.html#responsible-document
    virtual RefPtr<DOM::Document> responsible_document() = 0;

    // https://html.spec.whatwg.org/multipage/webappapis.html#api-url-character-encoding
    virtual String api_url_character_encoding() = 0;

    // https://html.spec.whatwg.org/multipage/webappapis.html#api-base-url
    virtual AK::URL api_base_url() = 0;

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-origin
    virtual Origin origin() = 0;

    // FIXME: A policy container https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-policy-container

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-cross-origin-isolated-capability
    virtual CanUseCrossOriginIsolatedAPIs cross_origin_isolated_capability() = 0;

    // https://html.spec.whatwg.org/multipage/webappapis.html#environment-settings-object%27s-realm
    JS::Realm& realm()
    {
        // An environment settings object's realm execution context's Realm component is the environment settings object's Realm.
        return *realm_execution_context().realm;
    }

    // https://html.spec.whatwg.org/multipage/webappapis.html#concept-settings-object-global
    JS::GlobalObject& global_object()
    {
        // An environment settings object's Realm then has a [[GlobalObject]] field, which contains the environment settings object's global object.
        return realm().global_object();
    }

    // https://html.spec.whatwg.org/multipage/webappapis.html#responsible-event-loop
    EventLoop& responsible_event_loop()
    {
        // An environment settings object's responsible event loop is its global object's relevant agent's event loop.
        auto& vm = global_object().vm();
        return verify_cast<Bindings::WebEngineCustomData>(vm.custom_data())->event_loop;
    }
};

}
