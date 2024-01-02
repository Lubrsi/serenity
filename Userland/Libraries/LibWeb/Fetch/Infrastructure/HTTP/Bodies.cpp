/*
 * Copyright (c) 2022-2023, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/MainThreadVM.h>
#include <LibWeb/Fetch/BodyInit.h>
#include <LibWeb/Fetch/Infrastructure/HTTP/Bodies.h>
#include <LibWeb/Fetch/Infrastructure/Task.h>
#include <LibWeb/WebIDL/Promise.h>
#include <LibWeb/Streams/ReadableStreamDefaultReader.h>
#include <LibWeb/HTML/Scripting/TemporaryExecutionContext.h>

namespace Web::Fetch::Infrastructure {

JS_DEFINE_ALLOCATOR(Body);

JS::NonnullGCPtr<Body> Body::create(JS::VM& vm, JS::NonnullGCPtr<Streams::ReadableStream> stream)
{
    return vm.heap().allocate_without_realm<Body>(stream);
}

JS::NonnullGCPtr<Body> Body::create(JS::VM& vm, JS::NonnullGCPtr<Streams::ReadableStream> stream, SourceType source, Optional<u64> length)
{
    return vm.heap().allocate_without_realm<Body>(stream, source, length);
}

Body::Body(JS::NonnullGCPtr<Streams::ReadableStream> stream)
    : m_stream(move(stream))
{
}

Body::Body(JS::NonnullGCPtr<Streams::ReadableStream> stream, SourceType source, Optional<u64> length)
    : m_stream(move(stream))
    , m_source(move(source))
    , m_length(move(length))
{
}

void Body::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_stream);
}

// https://fetch.spec.whatwg.org/#concept-body-clone
JS::NonnullGCPtr<Body> Body::clone(JS::Realm& realm) const
{
    // To clone a body body, run these steps:
    // FIXME: 1. Let « out1, out2 » be the result of teeing body’s stream.
    // FIXME: 2. Set body’s stream to out1.
    auto out2 = realm.heap().allocate<Streams::ReadableStream>(realm, realm);

    // 3. Return a body whose stream is out2 and other members are copied from body.
    return Body::create(realm.vm(), out2, m_source, m_length);
}

// https://fetch.spec.whatwg.org/#body-fully-read
WebIDL::ExceptionOr<void> Body::fully_read(Web::Fetch::Infrastructure::Body::ProcessBodyCallback process_body, Web::Fetch::Infrastructure::Body::ProcessBodyErrorCallback process_body_error, TaskDestination task_destination) const
{
    // FIXME: 1. If taskDestination is null, then set taskDestination to the result of starting a new parallel queue.
    // FIXME: Handle 'parallel queue' task destination
    VERIFY(!task_destination.has<Empty>());
    auto task_destination_object = task_destination.get<JS::NonnullGCPtr<JS::Object>>();

    // 2. Let successSteps given a byte sequence bytes be to queue a fetch task to run processBody given bytes, with taskDestination.
    auto success_steps = [process_body = move(process_body), task_destination_object = JS::make_handle(task_destination_object)](ByteBuffer bytes) mutable -> void {
        queue_fetch_task(*task_destination_object, [process_body = move(process_body), bytes = move(bytes)]() {
            process_body(move(bytes));
        });
    };

    // 3. Let errorSteps optionally given an exception exception be to queue a fetch task to run processBodyError given exception, with taskDestination.
    auto error_steps = [process_body_error = move(process_body_error), task_destination_object = JS::make_handle(task_destination_object)](JS::Value error) mutable {
        auto& exception = verify_cast<WebIDL::DOMException>(error.as_object());
        queue_fetch_task(*task_destination_object, [process_body_error = move(process_body_error), exception = JS::make_handle(exception)]() {
            process_body_error(*exception);
        });
    };

    // 4. Let reader be the result of getting a reader for body’s stream. If that threw an exception, then run errorSteps with that exception and return.
    auto& environment_settings_object = Bindings::host_defined_environment_settings_object(m_stream->realm());
    HTML::TemporaryExecutionContext temporary_execution_context { environment_settings_object  };
    environment_settings_object.prepare_to_run_callback();

    auto reader_or_exception = m_stream->get_reader();
    if (reader_or_exception.is_exception()) {
        error_steps(reader_or_exception.release_error().get<JS::NonnullGCPtr<WebIDL::DOMException>>());
        return {};
    }

    auto reader = reader_or_exception.release_value().get<JS::NonnullGCPtr<Streams::ReadableStreamDefaultReader>>();

    // 5. Read all bytes from reader, given successSteps and errorSteps.
    auto result = reader->read_all_bytes(move(success_steps), move(error_steps));
    environment_settings_object.clean_up_after_running_callback();
    return result;
}

// https://fetch.spec.whatwg.org/#byte-sequence-as-a-body
WebIDL::ExceptionOr<JS::NonnullGCPtr<Body>> byte_sequence_as_body(JS::Realm& realm, ReadonlyBytes bytes)
{
    // To get a byte sequence bytes as a body, return the body of the result of safely extracting bytes.
    auto [body, _] = TRY(safely_extract_body(realm, bytes));
    return body;
}

}
