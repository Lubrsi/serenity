/*
* Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
* Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
*
* SPDX-License-Identifier: BSD-2-Clause
*/

#pragma once

#include <AK/String.h>

namespace Web::HTML {

class Origin {
public:
    static Origin from_url(AK::URL const& url);

    Origin();
    Origin(String const& scheme, String const& host, u16 port);

    bool is_opaque() const;

    String const& scheme() const { return m_scheme; }
    String const& host() const { return m_host; }
    u16 port() const { return m_port; }

    bool is_same_origin(Origin const& other) const;
    bool is_same_origin_domain(Origin const& other) const;

    String serialize() const;

    Optional<String> effective_domain() const;

    bool operator==(Origin const& other) const { return is_same_origin(other); }
    bool operator!=(Origin const& other) const { return !is_same_origin(other); }

private:
    String m_scheme;
    String m_host;
    u16 m_port { 0 };
};

}

namespace AK {
template<>
struct Traits<Web::HTML::Origin> : public GenericTraits<Web::HTML::Origin> {
    static unsigned hash(Web::HTML::Origin const& origin)
    {
        return pair_int_hash(origin.scheme().hash(), pair_int_hash(int_hash(origin.port()), origin.host().hash()));
    }
};
} // namespace AK
