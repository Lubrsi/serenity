/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/URL.h>
#include <LibWeb/HTML/Origin.h>

namespace Web::HTML {

Origin::Origin() = default;
Origin::Origin(String const& scheme, String const& host, u16 port)
   : m_scheme(scheme)
   , m_host(host)
   , m_port(port)
{
}

// https://url.spec.whatwg.org/#concept-url-origin
Origin Origin::from_url(AK::URL const& url)
{
   if (url.scheme() == "blob"sv) {
       // FIXME: Implement
       return HTML::Origin {};
   }

   if (url.scheme().is_one_of("ftp"sv, "http"sv, "https"sv, "ws"sv, "wss"sv)) {
       // Return the tuple origin (url’s scheme, url’s host, url’s port, null).
       return HTML::Origin(url.scheme(), url.host(), url.port().value_or(0));
   }

   if (url.scheme() == "file"sv) {
       // Unfortunate as it is, this is left as an exercise to the reader. When in doubt, return a new opaque origin.
       // Note: We must return an origin with the `file://' protocol for `file://' iframes to work from `file://' pages.
       return HTML::Origin(url.scheme(), String(), 0);
   }

   return HTML::Origin {};
}

// https://html.spec.whatwg.org/multipage/origin.html#concept-origin-opaque
bool Origin::is_opaque() const
{
   return m_scheme.is_null() && m_host.is_null() && m_port == 0;
}

// https://html.spec.whatwg.org/multipage/origin.html#same-origin
bool Origin::is_same_origin(Web::HTML::Origin const& other) const
{
   // 1. If A and B are the same opaque origin, then return true.
   if (is_opaque() && other.is_opaque())
       return true;

   // 2. If A and B are both tuple origins and their schemes, hosts, and port are identical, then return true.
   // 3. Return false.
   return scheme() == other.scheme()
       && host() == other.host()
       && port() == other.port();
}

// https://html.spec.whatwg.org/multipage/origin.html#same-origin-domain
bool Origin::is_same_origin_domain(Web::HTML::Origin const& other) const
{
   // 1. If A and B are the same opaque origin, then return true.
   if (is_opaque() && other.is_opaque())
       return true;

   // 2. If A and B are both tuple origins, run these substeps:
   if (!is_opaque() && !other.is_opaque()) {
       // 1. If A and B's schemes are identical, and their domains are identical and non-null, then return true.
       // FIXME: Check domains once supported.
       if (scheme() == other.scheme())
           return true;

       // 2. Otherwise, if A and B are same origin and their domains are identical and null, then return true.
       // FIXME: Check domains once supported.
       if (is_same_origin(other))
           return true;
   }

   // 3. Return false.
   return false;
}

// https://html.spec.whatwg.org/multipage/origin.html#ascii-serialisation-of-an-origin
String Origin::serialize() const
{
   // 1. If origin is an opaque origin, then return "null"
   if (is_opaque())
       return "null";

   // 2. Otherwise, let result be origin's scheme.
   StringBuilder result;
   result.append(scheme());

   // 3. Append "://" to result.
   result.append("://"sv);

   // 4. Append origin's host, serialized, to result.
   result.append(host());

   // 5. If origin's port is non-null, append a U+003A COLON character (:), and origin's port, serialized, to result.
   if (port() != 0) {
       result.append(':');
       result.append(String::number(port()));
   }
   // 6. Return result
   return result.to_string();
}

// https://html.spec.whatwg.org/multipage/origin.html#concept-origin-effective-domain
Optional<String> Origin::effective_domain() const
{
    // 1. If origin is an opaque origin, then return null.
    if (is_opaque())
        return Optional<String> {};

    // FIXME: 2. If origin's domain is non-null, then return origin's domain.

    // 3. Return origin's host.
    return m_host;
}

}
