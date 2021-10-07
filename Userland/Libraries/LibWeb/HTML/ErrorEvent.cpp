/*
 * Copyright (c) 2021, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibWeb/HTML/ErrorEvent.h>

namespace Web::HTML {

ErrorEvent::ErrorEvent(FlyString const& event_name, ErrorEventInit const& event_init)
    : DOM::Event(event_name)
    , m_message(event_init.message)
    , m_filename(event_init.filename)
    , m_lineno(event_init.lineno)
    , m_colno(event_init.colno)
    , m_error(event_init.error)
{
}

void ErrorEvent::visit_edges(JS::Cell::Visitor& visitor)
{
    visitor.visit(m_error);
}

}
