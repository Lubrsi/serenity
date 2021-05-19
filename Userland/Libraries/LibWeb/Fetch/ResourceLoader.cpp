/*
 * Copyright (c) 2018-2021, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Base64.h>
#include <AK/Debug.h>
#include <AK/JsonObject.h>
#include <LibCore/EventLoop.h>
#include <LibCore/File.h>
#include <LibProtocol/Request.h>
#include <LibProtocol/RequestClient.h>
#include <LibWeb/Fetch/ContentFilter.h>
#include <LibWeb/Fetch/LoadRequest.h>
#include <LibWeb/Fetch/ResourceLoader.h>
#include <LibWeb/Fetch/Response.h>

namespace Web::Fetch {

ResourceLoader& ResourceLoader::the()
{
    static ResourceLoader* s_the;
    if (!s_the)
        s_the = &ResourceLoader::construct().leak_ref();
    return *s_the;
}

ResourceLoader::ResourceLoader()
    : m_protocol_client(Protocol::RequestClient::construct())
    , m_user_agent(default_user_agent)
{
}

void ResourceLoader::load_sync(const LoadRequest& request, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback)
{
    Core::EventLoop loop;

    load(
        request,
        [&](auto data, auto& response_headers, auto status_code) {
            success_callback(data, response_headers, status_code);
            loop.quit(0);
        },
        [&](auto& string, auto status_code) {
            if (error_callback)
                error_callback(string, status_code);
            loop.quit(0);
        });

    loop.exec();
}

static HashMap<LoadRequest, NonnullRefPtr<Response>> s_resource_cache;

RefPtr<Response> ResourceLoader::load_resource(Response::Type type, const LoadRequest& request)
{
    if (!request.is_valid())
        return nullptr;

    bool use_cache = request.url().protocol() != "file";

    if (use_cache) {
        auto it = s_resource_cache.find(request);
        if (it != s_resource_cache.end()) {
            if (it->value->type() != type) {
                dbgln("FIXME: Not using cached resource for {} since there's a type mismatch.", request.url());
            } else {
                dbgln_if(CACHE_DEBUG, "Reusing cached resource for: {}", request.url());
                return it->value;
            }
        }
    }

    auto resource = Response::create({}, type, request);

    if (use_cache)
        s_resource_cache.set(request, resource);

    load(
        request,
        [=](auto data, auto& headers, auto status_code) {
            const_cast<Response&>(*resource).did_load({}, data, headers, status_code);
        },
        [=](auto& error, auto status_code) {
            const_cast<Response&>(*resource).did_fail({}, error, status_code);
        });

    return resource;
}

void ResourceLoader::load(const LoadRequest& request, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback)
{
    auto& url = request.url();

    if (is_port_blocked(url.port())) {
        dbgln("ResourceLoader::load: Error: blocked port {} from URL {}", url.port(), url);
        return;
    }

    if (ContentFilter::the().is_filtered(url)) {
        dbgln("\033[32;1mResourceLoader::load: URL was filtered! {}\033[0m", url);
        error_callback("URL was filtered", {});
        return;
    }

    if (url.protocol() == "about") {
        dbgln("Loading about: URL {}", url);
        deferred_invoke([success_callback = move(success_callback)](auto&) {
            success_callback(String::empty().to_byte_buffer(), {}, {});
        });
        return;
    }

    if (url.protocol() == "data") {
        dbgln("ResourceLoader loading a data URL with mime-type: '{}', base64={}, payload='{}'",
            url.data_mime_type(),
            url.data_payload_is_base64(),
            url.data_payload());

        ByteBuffer data;
        if (url.data_payload_is_base64())
            data = decode_base64(url.data_payload());
        else
            data = url.data_payload().to_byte_buffer();

        deferred_invoke([data = move(data), success_callback = move(success_callback)](auto&) {
            success_callback(data, {}, {});
        });
        return;
    }

    if (url.protocol() == "file") {
        auto f = Core::File::construct();
        f->set_filename(url.path());
        if (!f->open(Core::OpenMode::ReadOnly)) {
            dbgln("ResourceLoader::load: Error: {}", f->error_string());
            if (error_callback)
                error_callback(f->error_string(), {});
            return;
        }

        auto data = f->read_all();
        deferred_invoke([data = move(data), success_callback = move(success_callback)](auto&) {
            success_callback(data, {}, {});
        });
        return;
    }

    if (url.protocol() == "http" || url.protocol() == "https" || url.protocol() == "gemini") {
        HashMap<String, String> headers;
        headers.set("User-Agent", m_user_agent);
        headers.set("Accept-Encoding", "gzip, deflate");

        for (auto& it : request.headers()) {
            headers.set(it.name, it.value);
        }

        auto protocol_request = protocol_client().start_request(request.method(), url.to_string_encoded(), headers, request.body());
        if (!protocol_request) {
            if (error_callback)
                error_callback("Failed to initiate load", {});
            return;
        }
        protocol_request->on_buffered_request_finish = [this, success_callback = move(success_callback), error_callback = move(error_callback), protocol_request](bool success, auto, auto& response_headers, auto status_code, ReadonlyBytes payload) {
            --m_pending_loads;
            if (on_load_counter_change)
                on_load_counter_change();
            if (!success) {
                if (error_callback)
                    error_callback("HTTP load failed", {});
                return;
            }
            deferred_invoke([protocol_request](auto&) {
                // Clear circular reference of `protocol_request` captured by copy
                const_cast<Protocol::Request&>(*protocol_request).on_buffered_request_finish = nullptr;
            });
            success_callback(payload, response_headers, status_code);
        };
        protocol_request->set_should_buffer_all_input(true);
        protocol_request->on_certificate_requested = []() -> Protocol::Request::CertificateAndKey {
            return {};
        };
        ++m_pending_loads;
        if (on_load_counter_change)
            on_load_counter_change();
        return;
    }

    if (error_callback)
        error_callback(String::formatted("Protocol not implemented: {}", url.protocol()), {});
}

void ResourceLoader::load(const URL& url, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback)
{
    LoadRequest request;
    request.set_url(url);
    load(request, move(success_callback), move(error_callback));
}

bool ResourceLoader::is_port_blocked(const URL& url)
{
    if (!url.is_http_or_https())
        return false;
    u16 ports[] { 1, 7, 9, 11, 13, 15, 17, 19, 20, 21, 22, 23, 25, 37, 42,
        43, 53, 69, 77, 79, 87, 95, 101, 102, 103, 104, 109, 110, 111, 113,
        115, 117, 119, 123, 135, 137, 139, 143, 179, 389, 427, 465, 512, 513, 514,
        515, 526, 530, 531, 532, 540, 548, 554, 556, 563, 587, 601, 636, 993, 995,
        1719, 1720, 1723, 2049, 3659, 4045, 5060, 5061, 6000, 6566, 6665, 6666, 6667, 6668, 6669, 6697, 10080 };
    for (auto blocked_port : ports)
        if (url.port() == blocked_port)
            return true;
    return false;
}

void ResourceLoader::clear_cache()
{
    dbgln("Clearing {} items from ResourceLoader cache", s_resource_cache.size());
    s_resource_cache.clear();
}

// https://fetch.spec.whatwg.org/#concept-fetch
// FIXME: This should contain an instance of the fetch algorithm. This instance can be terminated, suspended and resumed.
void ResourceLoader::fetch(LoadRequest& request, ProcessRequestBodyType process_request_body, ProcessRequestEndOfBodyType process_request_end_of_body, ProcessReponseType process_response, ProcessResponseEndOfBodyType process_response_end_of_body, ProcessResponseDoneType process_response_done, [[maybe_unused]] bool use_parallel_queue)
{
    // FIXME: Let taskDestination be null.

    // FIXME: Let crossOriginIsolatedCapability be false.

    // FIXME: If request’s client is non-null, then:
    //          Set taskDestination to request’s client’s global object.
    //          Set crossOriginIsolatedCapability to request’s client’s cross-origin isolated capability.

    // FIXME: If useParallelQueue is true, then set taskDestination to the result of starting a new parallel queue.

    // FIXME: Let timingInfo be a new fetch timing info whose start time and post-redirect start time are the coarsened shared current time given crossOriginIsolatedCapability.

    FetchParams fetch_params = {
        .request = request,
        // FIXME: timing info is timingInfo
        .process_request_body = process_request_body,
        .process_request_end_of_body = process_request_end_of_body,
        .process_response = process_response,
        .process_response_end_of_body = process_response_end_of_body,
        .process_response_done = process_response_done
        // FIXME: task destination is taskDestination
        // FIXME: cross-origin isolated capability is crossOriginIsolatedCapability
    };

    // FIXME: If request’s body is a byte sequence, then set request’s body to the first return value of safely extracting request’s body.
    // FIXME: If request’s window is "client", then set request’s window to request’s client, if request’s client’s global object is a Window object; otherwise "no-window".
    // FIXME: If request’s origin is "client", then set request’s origin to request’s client’s origin.
    // FIXME: If request’s policy container is "client", then:
    //          If request’s client is non-null, then set request’s policy container to a clone of request’s client’s policy container. [HTML]
    //          Otherwise, set request’s policy container to a new policy container.

    if (!request.headers().contains("Accept")) {
        String value = "*/*";

        switch (request.destination()) {
        case LoadRequest::Destination::Document:
        case LoadRequest::Destination::Frame:
        case LoadRequest::Destination::IFrame:
            value = "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8";
            break;
        case LoadRequest::Destination::Image:
            value = "image/png,image/svg+xml,image/*;q=0.8,*/*;q=0.5";
            break;
        case LoadRequest::Destination::Style:
            value = "text/css,*/*;q=0.1";
            break;
        }

        request.append_header("Accept", value);
    }

    // FIXME: If request’s header list does not contain `Accept-Language`, then user agents should append `Accept-Language`/an appropriate value to request’s header list.
    // FIXME: If request’s priority is null, then use request’s initiator and destination appropriately in setting request’s priority to a user-agent-defined object.

    // FIXME: If request is a subresource request, then:
    //          Let record be a new fetch record consisting of request and this instance of the fetch algorithm.
    //          Append record to request’s client’s fetch group list of fetch records.

    main_fetch(fetch_params);
}

// https://fetch.spec.whatwg.org/#concept-main-fetch
RefPtr<Response> ResourceLoader::main_fetch(const FetchParams& fetch_params, bool recursive)
{
    auto& request = fetch_params.request;
    RefPtr<Response> response;

    if (request.local_urls_only() && !request.current_url().is_local())
        response = Response::create_network_error({}, request);

    // FIXME: Run report Content Security Policy violations for request.
    // FIXME: Upgrade request to a potentially trustworthy URL, if appropriate.

    if (is_port_blocked(request.current_url()) /* FIXME: or should fetching request be blocked as mixed content, or should request be blocked by Content Security Policy */)
        response = Response::create_network_error({}, request);

    // FIXME: If request’s referrer policy is the empty string and request’s client is non-null, then set request’s referrer policy to request’s client’s referrer policy. [REFERRER]
    if (request.referrer_policy() == ReferrerPolicy::ReferrerPolicy::None) {
        // This is the default referrer policy. https://w3c.github.io/webappsec-referrer-policy/#default-referrer-policy
        request.set_referrer_policy({}, ReferrerPolicy::ReferrerPolicy::StrictOriginWhenCrossOrigin);
    }

    // FIXME: If request’s referrer is not "no-referrer", then set request’s referrer to the result of invoking determine request’s referrer. [REFERRER]
    // FIXME: Set request’s current URL’s scheme to "https" if all of the following conditions are true:
    //          request’s current URL’s scheme is "http"
    //          request’s current URL’s host is a domain
    //          Matching request’s current URL’s host per Known HSTS Host Domain Name Matching results in either a superdomain match with an asserted includeSubDomains directive or a congruent match (with or without an asserted includeSubDomains directive). [HSTS]

    auto do_fetch = [this, &fetch_params, &request]() -> RefPtr<Response> {
        auto& current_url = request.current_url();
        auto current_url_origin = Origin::create_from_url(current_url);
        auto& request_origin_variant = request.origin();
        VERIFY(request_origin_variant.template has<Origin>());
        auto& request_origin = request_origin_variant.template get<Origin>();

        if ((current_url_origin.is_same(request_origin) && request.response_tainting() == LoadRequest::ResponseTainting::Basic)
            || current_url.protocol() == "data"
            || (request.mode() == LoadRequest::Mode::Navigate || request.mode() == LoadRequest::Mode::WebSocket)) {
            request.set_response_tainting({}, LoadRequest::ResponseTainting::Basic);
            return scheme_fetch(fetch_params);
        }

        if (request.mode() == LoadRequest::Mode::SameOrigin)
            return Response::create_network_error({}, request);

        if (request.mode() == LoadRequest::Mode::NoCors) {
            if (request.redirect_mode() != LoadRequest::RedirectMode::Follow)
                return Response::create_network_error({}, request);

            request.set_response_tainting({}, LoadRequest::ResponseTainting::Opaque);

            // FIXME: This should be done out of process to prevent side channel attacks!
            auto no_cors_response = scheme_fetch(fetch_params);
            // FIXME: If noCorsResponse is a filtered response or the CORB check with request and noCorsResponse returns allowed, then return noCorsResponse.
            // FIXME: Return a new response whose status is noCorsResponse’s status.
            return Response::create_network_error({}, request);
        }

        if (!current_url.is_http_or_https())
            return Response::create_network_error({}, request);

//      FIXME  if (request.use_cors_preflight()
//            || (request.unsafe_request() && (is_cors_safelisted_method(request.method()) || )))

        request.set_response_tainting({}, LoadRequest::ResponseTainting::Cors);
        return http_fetch(fetch_params);
    };

    if (!recursive) {
        // FIXME: This should be in parallel.
        deferred_invoke([&](auto&) {
            if (!response)
                response = do_fetch();


        });
    } else {
        if (!response)
            response = do_fetch();
        return response;
    }
}

RefPtr<Response> ResourceLoader::scheme_fetch(const FetchParams& fetch_params)
{
    auto& request = fetch_params.request;
    auto& url = request.current_url();

    if (url.protocol() == "about") {
        // FIXME:
        dbgln("Loading about: URL {}", url);
        deferred_invoke([success_callback = move(success_callback)](auto&) {
            success_callback(String::empty().to_byte_buffer(), {}, {});
        });
        return Response::create_network_error({}, request);
    }

    // FIXME: Handle blob.

    if (url.protocol() == "data") {
        dbgln("ResourceLoader loading a data URL with mime-type: '{}', base64={}, payload='{}'",
              url.data_mime_type(),
              url.data_payload_is_base64(),
              url.data_payload());

        // FIXME: This is a lot more involved.
        ByteBuffer data;
        if (url.data_payload_is_base64())
            data = decode_base64(url.data_payload());
        else
            data = url.data_payload().to_byte_buffer();

        // FIXME
        return Response::create_network_error({}, request);
    }

    if (url.protocol() == "file") {
        // NOTE: This is implementation defined.
        auto f = Core::File::construct();
        f->set_filename(url.path());
        if (!f->open(Core::OpenMode::ReadOnly)) {
            dbgln("ResourceLoader::scheme_fetch: Error: {}", f->error_string());
            return Response::create_network_error({}, request);
        }

        auto data = f->read_all();
        //Response::create()
        // FIXME.
        return Response::create_network_error({}, request);
    }

    // FIXME: Handle gemini.
    if (url.is_http_or_https())
        return http_fetch(fetch_params);

    return Response::create_network_error({}, request);
}

// https://fetch.spec.whatwg.org/#concept-http-fetch
RefPtr<Response> ResourceLoader::http_fetch(const FetchParams& fetch_params, bool make_cors_preflight)
{
    auto& request = fetch_params.request;
    RefPtr<Response> response;
    RefPtr<Response> actual_response;
    auto& timing_info = fetch_params.timing_info;

    // FIXME: If request’s service-workers mode is "all", then:

    if (!response) {
        // FIXME: If makeCORSPreflight is true and one of these conditions is true:

        if (request.redirect_mode() == LoadRequest::RedirectMode::Follow)
            request.set_service_workers_mode({}, LoadRequest::ServiceWorkersMode::None);

        response = actual_response = http_network_or_cache_fetch(fetch_params);

        if (request.response_tainting() == LoadRequest::ResponseTainting::Cors && !cors_check(request, response))
            return Response::create_network_error({}, request);

        if (!tao_check(request, response))
            request.set_timing_allow_failed({}, true);
    }

    // FIXME: Cross-origin resource policy
    //if ((request.response_tainting() == LoadRequest::ResponseTainting::Opaque || response->new_type() == Response::NewType::Opaque))

    if (actual_response->has_redirect_status()) {
        // FIXME: If actualResponse’s status is not 303, request’s body is not null, and the connection uses HTTP/2, then user agents may, and are even encouraged to, transmit an RST_STREAM frame.

        switch (request.redirect_mode()) {
        case LoadRequest::RedirectMode::Error:
            response = Response::create_network_error({}, request);
            break;
        case LoadRequest::RedirectMode::Manual:
            // FIXME: Set response to an opaque-redirect filtered response whose internal response is actualResponse.
            TODO();
            break;
        case LoadRequest::RedirectMode::Follow:
            response = http_redirect_fetch(fetch_params, response);
            break;
        }
    }

    response->set_timing_info({}, timing_info);

    return response;
}

// https://fetch.spec.whatwg.org/#concept-http-network-or-cache-fetch
RefPtr<Response> ResourceLoader::http_network_or_cache_fetch(const FetchParams& fetch_params, bool is_authentication_fetch, bool is_new_connection_fetch)
{
    auto& request = fetch_params.request;
    // FIXME: Let httpFetchParams be null.
    // FIXME: Let httpRequest be null.
    RefPtr<Response> response;
    // FIXME: Let storedResponse be null.
    // FIXME: Let httpCache be null.
    bool revalidating_flag = false;

    // FIXME: Run these steps, but abort when the ongoing fetch is terminated:
    // Here, we are setting up all the HTTP headers as per the spec and potentially returning a cached response.
    // FIXME: If request’s window is "no-window" and request’s redirect mode is "error", then set httpFetchParams to fetchParams and httpRequest to request.
    // FIXME: Otherwise:
    //          Set httpRequest to a clone of request.
    //          Set httpFetchParams to a copy of fetchParams.
    //          Set httpFetchParams’s request to httpRequest.

    bool include_credentials = request.credentials_mode() == LoadRequest::CredentialsMode::Include || (request.credentials_mode() == LoadRequest::CredentialsMode::SameOrigin && request.response_tainting() == LoadRequest::ResponseTainting::Basic);
    // FIXME: Let contentLength be httpRequest’s body’s length, if httpRequest’s body is non-null; otherwise null.
    Optional<size_t> content_length;
    String content_length_header_value; // FIXME: This should be a byte sequence.

    // FIXME: Let contentLengthHeaderValue be null.
    // FIXME: If httpRequest’s body is null and httpRequest’s method is `POST` or `PUT`, then set contentLengthHeaderValue to `0`.

    if (content_length.has_value()) {
        // FIXME: And isomorphic encoded.
        content_length_header_value = String::number(content_length.value());
    }

    if (!content_length_header_value.is_null())
        request.append_header("Content-Length", content_length_header_value);

    // FIXME: If contentLength is non-null and httpRequest’s keepalive is true, then:

    if (request.referrer().has<URL>()) {
        // FIXME: Isomorphic encode the serialized URL.
        // FIXME: Is this an encoded URL?
        request.append_header("Referer", request.referrer().get<URL>().to_string_encoded());
    }

    // https://fetch.spec.whatwg.org/#append-a-request-origin-header
    auto serialized_origin = request.serialize_origin(); // FIXME: Byte serialized.

    if (request.response_tainting() == LoadRequest::ResponseTainting::Cors || request.mode() == LoadRequest::Mode::WebSocket) {
        request.append_header("Origin", serialized_origin);
    } else if (!request.method().is_one_of("GET", "HEAD")) {
        switch (request.referrer_policy()) {
        case ReferrerPolicy::ReferrerPolicy::NoReferrer:
            serialized_origin = "null";
            break;
        case ReferrerPolicy::ReferrerPolicy::NoReferrerWhenDowngrade:
        case ReferrerPolicy::ReferrerPolicy::StrictOrigin:
        case ReferrerPolicy::ReferrerPolicy::StrictOriginWhenCrossOrigin: {
            auto& request_origin = request.origin().get<Origin>();
            if (!request_origin.is_null() && request_origin.protocol() == "https" && request.current_url().protocol() != "https")
                serialized_origin = "null";
            break;
        }
        case ReferrerPolicy::ReferrerPolicy::SameOrigin: {
            auto& request_origin = request.origin().get<Origin>();
            auto current_url_origin = Origin::create_from_url(request.current_url());
            if (!request_origin.is_same(current_url_origin))
                serialized_origin = "null";
            break;
        }
        default:
            break;
        }

        request.append_header("Origin", serialized_origin);
    }

    // == End of append origin header ==

    // FIXME: Append the Fetch metadata headers for httpRequest.

    if (!request.headers().contains("User-Agent"))
        request.append_header("User-Agent", default_user_agent);

    if (request.cache_mode() == LoadRequest::CacheMode::Default && (request.headers().contains("If-Modified-Since") || request.headers().contains("If-None-Match") || request.headers().contains("If-Unmodified-Since") || request.headers().contains("If-Match") || request.headers().contains("If-Range")))
        request.set_cache_mode({}, LoadRequest::CacheMode::NoStore);

    if (request.cache_mode() == LoadRequest::CacheMode::NoStore && !request.prevent_no_cache_cache_control_header_modification() && !request.headers().contains("Cache-Control"))
        request.append_header("Cache-Control", "max-age=0");

    if (request.headers().contains("Range"))
        request.append_header("Accept-Encoding", "identity");

    // Step 18: Modify httpRequest’s header list per HTTP. Do not append a given header if httpRequest’s header list contains that header’s name.
    // This is where we start inserting headers such as Accept-Encoding, Connection, etc.
    if (!request.headers().contains("Accept-Encoding"))
        request.append_header("Accept-Encoding", "gzip, deflate");

    if (include_credentials) {
        // FIXME: Append cookies here instead of create_request_for_page

        if (!request.headers().contains("Authorization")) {
            // FIXME: Let authorizationValue be null.
            // FIXME: If there’s an authentication entry for httpRequest and either httpRequest’s use-URL-credentials flag is unset or httpRequest’s current URL does not include credentials, then set authorizationValue to authentication entry.
            // FIXME: Otherwise, if httpRequest’s current URL does include credentials and isAuthenticationFetch is true, set authorizationValue to httpRequest’s current URL, converted to an `Authorization` value.
            // FIXME: If authorizationValue is non-null, then append `Authorization`/authorizationValue to httpRequest’s header list.
        }
    }

    // FIXME: If there’s a proxy-authentication entry, use it as appropriate. (yes, that's what it says)
    // FIXME: Set httpCache to the result of determining the HTTP cache partition, given httpRequest.
    // FIXME: If httpCache is null, then set httpRequest’s cache mode to "no-store".
    // FIXME: If httpRequest’s cache mode is neither "no-store" nor "reload", then:

    // FIXME: If aborted, then:
    //          Let aborted be the termination’s aborted flag.
    //          If aborted is set, then return an aborted network error.
    //          Return a network error.

    // Response can still be null here as all the stuff before was setting up the request and then potentially
    // getting it from cache. No network fetching has happened yet at this point.
    if (!response) {
        if (request.cache_mode() == LoadRequest::CacheMode::OnlyIfCached)
            return Response::create_network_error({}, request);

        auto forward_response = http_network_fetch(fetch_params /* FIXME: httpFetchParams */, include_credentials, is_new_connection_fetch);

        // FIXME: If httpRequest’s method is unsafe and forwardResponse’s status is in the range 200 to 399, inclusive, invalidate appropriate stored responses in httpCache, as per the "Invalidation" chapter of HTTP Caching, and set storedResponse to null. [HTTP-CACHING]
        // FIXME: More stuff.....
    }
}

RefPtr<Response> ResourceLoader::http_network_fetch(const FetchParams& fetch_params, bool include_credentials, bool force_new_connection)
{
    auto& request = fetch_params.request;
    RefPtr<Response> response;
    // FIXME: Let timingInfo be fetchParams’s timing info.
    // FIXME: Let httpCache be the result of determining the HTTP cache partition, given httpRequest.
    // FIXME: If httpCache is null: (which it always is currently)
    request.set_cache_mode(LoadRequest::CacheMode::NoStore);
    // Let networkPartitionKey be the result of determining the network partition key given request.

    // FIXME: If mode is websocket, obtain a WebSocket connection, given request's current URL.
    // FIXME: Otherwise: Let connection be the result of obtaining a connection, given networkPartitionKey, request’s current URL’s origin, includeCredentials, and forceNewConnection.

    // FIXME: Set timingInfo’s final connection timing info to the result of calling clamp and coarsen connection timing info with connection’s timing info, timingInfo’s post-redirect start time, and fetchParams’s cross-origin isolated capability.

    // FIXME: Run these steps, but abort when the ongoing fetch is terminated:
}

RefPtr<Response> ResourceLoader::http_redirect_fetch(const FetchParams&, RefPtr<Response>)
{

}

// https://fetch.spec.whatwg.org/#concept-cors-check
// Returns false for failure, true for success.
bool ResourceLoader::cors_check(const LoadRequest& request, RefPtr<Response> response)
{
    auto origin = response->header_list().get("Access-Control-Allow-Origin");
    if (origin.is_null())
        return false;

    if (request.credentials_mode() != LoadRequest::CredentialsMode::Include && origin == "*")
        return true;

    // FIXME: This should be byte serialized.
    if (request.serialize_origin() != origin)
        return false;

    if (request.credentials_mode() != LoadRequest::CredentialsMode::Include)
        return true;

    auto credentials = response->header_list().get("Access-Control-Allow-Credentials");
    if (credentials == "true")
        return true;

    return false;
}

// https://fetch.spec.whatwg.org/#concept-tao-check
// Returns false for failure, true for success.
bool ResourceLoader::tao_check(const LoadRequest& load_request, RefPtr<Response> response)
{
    if (load_request.timing_allow_failed())
        return false;

    if (load_request.response_tainting() == LoadRequest::ResponseTainting::Basic)
        return true;

    // FIXME
    return false;
}

}
