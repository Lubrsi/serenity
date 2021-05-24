/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/HashMap.h>
#include <AK/URL.h>
#include <AK/Tuple.h>
#include <AK/Variant.h>
#include <AK/WeakPtr.h>
#include <LibHTTP/HeaderList.h>
#include <LibWeb/Forward.h>
#include <LibWeb/HTML/PolicyContainer.h>
#include <LibWeb/Origin.h>
#include <LibWeb/ReferrerPolicy/ReferrerPolicy.h>
#include <LibWeb/Page/Page.h>

namespace Web::Fetch {

bool is_forbidden_method(const String& method);
String normalize_method(const String& method);
bool is_cors_safelisted_method(const String& method);
bool is_safe_method(const String& method);

// https://fetch.spec.whatwg.org/#concept-request
class LoadRequest {
public:
    // https://fetch.spec.whatwg.org/#concept-request-window
    enum class Window {
        NoWindow,
        Client,
    };

    // https://fetch.spec.whatwg.org/#request-service-workers-mode
    enum class ServiceWorkersMode {
        All,
        None,
    };

    // https://fetch.spec.whatwg.org/#concept-request-initiator
    enum class Initiator {
        None,
        Download,
        ImageSet,
        Manifest,
        Prefetch,
        Prerender,
        XSLT,
    };

    // https://fetch.spec.whatwg.org/#concept-request-destination
    enum class Destination {
        None,
        Audio,
        AudioWorklet,
        Document,
        Embed,
        Font,
        Frame,
        IFrame,
        Image,
        Manifest,
        Object,
        PaintWorklet,
        Report,
        Script,
        ServiceWorker,
        SharedWorker,
        Style,
        Track,
        Video,
        Worker,
        XSLT,
    };

    // https://fetch.spec.whatwg.org/#concept-request-origin
    enum class OriginEnum {
        Client,
    };

    // https://fetch.spec.whatwg.org/#concept-request-policy-container
    enum class PolicyContainerEnum {
        Client,
    };

    // https://fetch.spec.whatwg.org/#concept-request-referrer
    enum class Referrer {
        NoReferrer,
        Client,
    };

    // https://fetch.spec.whatwg.org/#concept-request-mode
    enum class Mode {
        SameOrigin,
        Cors,
        NoCors,
        Navigate,
        WebSocket,
    };

    // https://fetch.spec.whatwg.org/#concept-request-credentials-mode
    enum class CredentialsMode {
        Omit,
        SameOrigin,
        Include,
    };

    // https://fetch.spec.whatwg.org/#concept-request-cache-mode
    enum class CacheMode {
        Default,
        NoStore,
        Reload,
        NoCache,
        ForceCache,
        OnlyIfCached,
    };

    // https://fetch.spec.whatwg.org/#concept-request-redirect-mode
    enum class RedirectMode {
        Follow,
        Error,
        Manual,
    };

    // https://fetch.spec.whatwg.org/#concept-request-parser-metadata
    enum class ParserMetadata {
        None,
        ParserInserted,
        NotParserInserted,
    };

    // https://fetch.spec.whatwg.org/#concept-request-response-tainting
    enum class ResponseTainting {
        Basic,
        Cors,
        Opaque,
    };

    using AuthenticationEntry = Tuple<String, String, String>; // Tuple of username, password and realm.

    LoadRequest(const URL& url, Page* page)
    {
        m_url_list.append(url);
        m_client = page;
    }

    static LoadRequest create_a_potential_cors_request(const URL& url, Page* page, Destination destination /* FIXME: and corsAttributeState and an optional same-origin fallback flag */);

    static LoadRequest create_for_url_on_page(const URL& url, Page* page);

    bool is_valid() const { return url().is_valid(); }

    const URL& url() const { return m_url_list.first(); }
    void set_url(const URL& url) { m_url_list[0] = url; }

    const String& method() const { return m_method; }
    void set_method(const String& method) { m_method = method; }

    const ByteBuffer& body() const { return m_body; }
    void set_body(const ByteBuffer& body) { m_body = body; }

    unsigned hash() const
    {
        // FIXME: Include headers in the hash as well
        return pair_int_hash(pair_int_hash(url().to_string().hash(), m_method.hash()), string_hash((const char*)m_body.data(), m_body.size()));
    }

    bool operator==(const LoadRequest& other) const
    {
        if (m_headers.size() != other.m_headers.size())
            return false;
        for (auto& it : m_headers) {
            auto jt = other.m_headers.get(it.name);
            if (jt.is_null())
                return false;
            if (it.value != jt)
                return false;
        }
        return url() == other.url() && m_method == other.m_method && m_body == other.m_body;
    }

    void set_header(const String& name, const String& value) { m_headers.set(name, value); }
    void append_header(const String& name, const String& value) { m_headers.append(name, value); }
    String header(const String& name) const { return m_headers.get(name); }

    const HTTP::HeaderList& headers() const { return m_headers; }

    // https://fetch.spec.whatwg.org/#concept-request-current-url
    const URL& current_url() const { return m_url_list.last(); }

    Destination destination() const { return m_destination; }

    // https://fetch.spec.whatwg.org/#request-destination-script-like
    bool destination_is_script_like() const
    {
        return m_destination == Destination::AudioWorklet
            || m_destination == Destination::PaintWorklet
            || m_destination == Destination::Script
            || m_destination == Destination::ServiceWorker
            || m_destination == Destination::SharedWorker
            || m_destination == Destination::Worker;
    }

    // https://fetch.spec.whatwg.org/#subresource-request
    bool is_subresource_request() const
    {
        return m_destination == Destination::Audio
            || m_destination == Destination::AudioWorklet
            || m_destination == Destination::Font
            || m_destination == Destination::Image
            || m_destination == Destination::Manifest
            || m_destination == Destination::PaintWorklet
            || m_destination == Destination::Script
            || m_destination == Destination::Style
            || m_destination == Destination::Track
            || m_destination == Destination::Video
            || m_destination == Destination::XSLT;
    }

    // https://fetch.spec.whatwg.org/#navigation-request
    bool is_navigation_request() const
    {
        return m_destination == Destination::Document
            || m_destination == Destination::Embed
            || m_destination == Destination::Frame
            || m_destination == Destination::IFrame
            || m_destination == Destination::Object;
    }

    bool local_urls_only() const { return m_local_urls_only; }

    ReferrerPolicy::ReferrerPolicy referrer_policy() const { return m_referrer_policy; }
    void set_referrer_policy(ReferrerPolicy::ReferrerPolicy referrer_policy) { m_referrer_policy = referrer_policy; }

    ResponseTainting response_tainting() const { return m_response_tainting; }
    void set_response_tainting(Badge<ResourceLoader>, ResponseTainting response_tainting) { m_response_tainting = response_tainting; }

    const Variant<OriginEnum, Origin>& origin() const { return m_origin; }
    void set_origin(const Origin& origin) { m_origin = Origin(origin); }

    Mode mode() const { return m_mode; }

    RedirectMode redirect_mode() const { return m_redirect_mode; }

    bool use_cors_preflight() const { return m_use_cors_preflight; }

    bool unsafe_request() const { return m_unsafe_request; }

    ServiceWorkersMode service_workers_mode() const { return m_service_workers_mode; }
    void set_service_workers_mode(ServiceWorkersMode service_workers_mode) { m_service_workers_mode = service_workers_mode; }

    CredentialsMode credentials_mode() const { return m_credentials_mode; }

    String serialize_origin() const;

    bool tainted_origin() const { return m_tainted_origin; }
    void set_tainted_origin(Badge<ResourceLoader>, bool tainted_origin) { m_tainted_origin = tainted_origin; }

    bool timing_allow_failed() const { return m_timing_allow_failed; }
    void set_timing_allow_failed(Badge<ResourceLoader>, bool timing_allow_failed) { m_timing_allow_failed = timing_allow_failed; }

    Variant<Referrer, URL> referrer() const { return m_referrer; }

    CacheMode cache_mode() const { return m_cache_mode; }
    void set_cache_mode(CacheMode cache_mode) { m_cache_mode = cache_mode; }

    bool prevent_no_cache_cache_control_header_modification() const { return m_prevent_no_cache_cache_control_header_modification; }

    bool has_authenication_entry() const
    {
        return !m_authentication_entry.get<0>().is_null() && !m_authentication_entry.get<1>().is_null() && !!m_authentication_entry.get<2>().is_null();
    }

    const Vector<URL>& url_list() const { return m_url_list; }

    Window window() const { return m_window; }
    void set_window(Window window) { m_window = window; }

    u8 redirect_count() const { return m_redirect_count; }
    void increment_redirect_count(Badge<ResourceLoader>) { ++m_redirect_count; }

    void append_url_to_url_list(Badge<ResourceLoader>, const URL& url) { m_url_list.append(url); }

    bool done() const { return m_done; }
    void set_done(Badge<ResourceLoader>, bool done) { m_done = done; }

    WeakPtr<Page> client() const { return m_client; }

private:
    String m_method { "GET" }; // FIXME: This should be a byte sequence.
    bool m_local_urls_only { false };
    HTTP::HeaderList m_headers;
    bool m_unsafe_request { false };
    ByteBuffer m_body; // FIXME: Or a body object
    WeakPtr<Page> m_client; // FIXME: this should be an environment settings object
    // FIXME: A request has an associated reserved client (null, an environment, or an environment settings object). Unless stated otherwise it is null.
    String m_replaces_client_id;
    Window m_window { Window::Client }; // FIXME: or an environment settings object whose global object is a Window object
    bool m_keep_alive { false };
    ServiceWorkersMode m_service_workers_mode { ServiceWorkersMode::All };
    Initiator m_initiator { Initiator::None };
    Destination m_destination { Destination::None };
    // FIXME: A request has an associated priority (null or a user-agent-defined object). Unless otherwise stated it is null.
    Variant<OriginEnum, Origin> m_origin { OriginEnum::Client };
    HTML::PolicyContainer m_policy_container;
    Variant<Referrer, URL> m_referrer { Referrer::Client };
    ReferrerPolicy::ReferrerPolicy m_referrer_policy { ReferrerPolicy::ReferrerPolicy::None };
    Mode m_mode { Mode::NoCors };
    bool m_use_cors_preflight { false };
    CredentialsMode m_credentials_mode { CredentialsMode::SameOrigin };
    bool m_use_url_credentials { false };
    CacheMode m_cache_mode { CacheMode::Default };
    RedirectMode m_redirect_mode { RedirectMode::Follow };
    String m_integrity_metadata;
    String m_crytographic_nonce_metadata;
    ParserMetadata m_parser_metadata { ParserMetadata::None };
    bool m_reload_navigation { false };
    bool m_history_navigation { false };
    bool m_user_activation { false };
    bool m_tainted_origin { false };
    Vector<URL> m_url_list;    // FIXME: Put in m_url as the first entry
    u8 m_redirect_count { 0 };
    ResponseTainting m_response_tainting { ResponseTainting::Basic };
    bool m_prevent_no_cache_cache_control_header_modification { false };
    bool m_done { false };
    bool m_timing_allow_failed { false };
    AuthenticationEntry m_authentication_entry { {}, {}, {} }; // Username, password and realm are null by default.
};

}

namespace AK {

template<>
struct Traits<Web::Fetch::LoadRequest> : public GenericTraits<Web::Fetch::LoadRequest> {
    static unsigned hash(const Web::Fetch::LoadRequest& request) { return request.hash(); }
};

}
