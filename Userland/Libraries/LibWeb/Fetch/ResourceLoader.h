/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Function.h>
#include <AK/URL.h>
#include <LibCore/Object.h>
#include <LibWeb/Fetch/Response.h>
#include <LibWeb/Fetch/FetchParams.h>

namespace Protocol {
class RequestClient;
}

namespace Web::Fetch {

constexpr auto default_user_agent = "Mozilla/4.0 (SerenityOS; x86) LibWeb+LibJS (Not KHTML, nor Gecko) LibWeb";

class ResourceLoader : public Core::Object {
    C_OBJECT(ResourceLoader)
public:
    static ResourceLoader& the();

    RefPtr<Response> load_resource(Response::Type, const LoadRequest&);

    void load(const LoadRequest&, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback = nullptr);
    void load(const URL&, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback = nullptr);
    void load_sync(const LoadRequest&, Function<void(ReadonlyBytes, const HashMap<String, String, CaseInsensitiveStringTraits>& response_headers, Optional<u32> status_code)> success_callback, Function<void(const String&, Optional<u32> status_code)> error_callback = nullptr);

    Function<void()> on_load_counter_change;

    int pending_loads() const { return m_pending_loads; }

    Protocol::RequestClient& protocol_client() { return *m_protocol_client; }

    const String& user_agent() const { return m_user_agent; }
    void set_user_agent(const String& user_agent) { m_user_agent = user_agent; }

    void clear_cache();

    void fetch(LoadRequest&, ProcessRequestBodyType, ProcessRequestEndOfBodyType, ProcessReponseType, ProcessResponseEndOfBodyType, ProcessResponseDoneType, bool use_parallel_queue = false);

private:
    ResourceLoader();
    static bool is_port_blocked(const URL&);

    RefPtr<Response> main_fetch(const FetchParams&, bool recursive = false);

    int m_pending_loads { 0 };

    RefPtr<Protocol::RequestClient> m_protocol_client;
    String m_user_agent;
};

}
