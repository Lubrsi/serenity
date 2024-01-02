/*
 * Copyright (c) 2022, Linus Groh <linusg@serenityos.org>
 * Copyright (c) 2023, Shannon Booth <shannon@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/PromiseCapability.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/Streams/AbstractOperations.h>
#include <LibWeb/Streams/ReadableByteStreamController.h>
#include <LibWeb/Streams/ReadableStream.h>
#include <LibWeb/Streams/ReadableStreamBYOBReader.h>
#include <LibWeb/Streams/ReadableStreamDefaultController.h>
#include <LibWeb/Streams/ReadableStreamDefaultReader.h>
#include <LibWeb/Streams/UnderlyingSource.h>
#include <LibWeb/WebIDL/ExceptionOr.h>
#include <LibJS/Runtime/TypedArray.h>
#include <LibJS/Runtime/DataView.h>
#include <LibWeb/Streams/ReadableStreamBYOBRequest.h>
#include <LibWeb/WebIDL/Buffers.h>

namespace Web::Streams {

JS_DEFINE_ALLOCATOR(ReadableStream);

// https://streams.spec.whatwg.org/#rs-constructor
WebIDL::ExceptionOr<JS::NonnullGCPtr<ReadableStream>> ReadableStream::construct_impl(JS::Realm& realm, Optional<JS::Handle<JS::Object>> const& underlying_source_object, QueuingStrategy const& strategy)
{
    auto& vm = realm.vm();

    auto readable_stream = realm.heap().allocate<ReadableStream>(realm, realm);

    // 1. If underlyingSource is missing, set it to null.
    auto underlying_source = underlying_source_object.has_value() ? JS::Value(underlying_source_object.value()) : JS::js_null();

    // 2. Let underlyingSourceDict be underlyingSource, converted to an IDL value of type UnderlyingSource.
    auto underlying_source_dict = TRY(UnderlyingSource::from_value(vm, underlying_source));

    // 3. Perform ! InitializeReadableStream(this).

    // 4. If underlyingSourceDict["type"] is "bytes":
    if (underlying_source_dict.type.has_value() && underlying_source_dict.type.value() == ReadableStreamType::Bytes) {
        // 1. If strategy["size"] exists, throw a RangeError exception.
        if (strategy.size)
            return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Size strategy not allowed for byte stream"sv };

        // 2. Let highWaterMark be ? ExtractHighWaterMark(strategy, 0).
        auto high_water_mark = TRY(extract_high_water_mark(strategy, 0));

        // 3. Perform ? SetUpReadableByteStreamControllerFromUnderlyingSource(this, underlyingSource, underlyingSourceDict, highWaterMark).
        TRY(set_up_readable_byte_stream_controller_from_underlying_source(*readable_stream, underlying_source, underlying_source_dict, high_water_mark));
    }
    // 5. Otherwise,
    else {
        // 1. Assert: underlyingSourceDict["type"] does not exist.
        VERIFY(!underlying_source_dict.type.has_value());

        // 2. Let sizeAlgorithm be ! ExtractSizeAlgorithm(strategy).
        auto size_algorithm = extract_size_algorithm(strategy);

        // 3. Let highWaterMark be ? ExtractHighWaterMark(strategy, 1).
        auto high_water_mark = TRY(extract_high_water_mark(strategy, 1));

        // 4. Perform ? SetUpReadableStreamDefaultControllerFromUnderlyingSource(this, underlyingSource, underlyingSourceDict, highWaterMark, sizeAlgorithm).
        TRY(set_up_readable_stream_default_controller_from_underlying_source(*readable_stream, underlying_source, underlying_source_dict, high_water_mark, move(size_algorithm)));
    }

    return readable_stream;
}

ReadableStream::ReadableStream(JS::Realm& realm)
    : PlatformObject(realm)
{
}

ReadableStream::~ReadableStream() = default;

// https://streams.spec.whatwg.org/#rs-locked
bool ReadableStream::locked() const
{
    // 1. Return ! IsReadableStreamLocked(this).
    return is_readable_stream_locked(*this);
}

// https://streams.spec.whatwg.org/#rs-cancel
WebIDL::ExceptionOr<JS::GCPtr<JS::Object>> ReadableStream::cancel(JS::Value reason)
{
    auto& realm = this->realm();

    // 1. If ! IsReadableStreamLocked(this) is true, return a promise rejected with a TypeError exception.
    if (is_readable_stream_locked(*this)) {
        auto exception = JS::TypeError::create(realm, "Cannot cancel a locked stream"sv);
        return WebIDL::create_rejected_promise(realm, JS::Value { exception })->promise();
    }

    // 2. Return ! ReadableStreamCancel(this, reason).
    return TRY(readable_stream_cancel(*this, reason))->promise();
}

// https://streams.spec.whatwg.org/#rs-get-reader
WebIDL::ExceptionOr<ReadableStreamReader> ReadableStream::get_reader(ReadableStreamGetReaderOptions const& options)
{
    // 1. If options["mode"] does not exist, return ? AcquireReadableStreamDefaultReader(this).
    if (!options.mode.has_value())
        return ReadableStreamReader { TRY(acquire_readable_stream_default_reader(*this)) };

    // 2. Assert: options["mode"] is "byob".
    VERIFY(*options.mode == Bindings::ReadableStreamReaderMode::Byob);

    // 3. Return ? AcquireReadableStreamBYOBReader(this).
    return ReadableStreamReader { TRY(acquire_readable_stream_byob_reader(*this)) };
}

// https://streams.spec.whatwg.org/#readablestream-enqueue
WebIDL::ExceptionOr<void> ReadableStream::enqueue(JS::Value chunk)
{
    auto& realm = this->realm();

    // 1. If stream.[[controller]] implements ReadableStreamDefaultController,
    VERIFY(m_controller.has_value());
    auto& controller = m_controller.value();
    if (controller.has<JS::NonnullGCPtr<ReadableStreamDefaultController>>()) {
        // 1. Perform ! ReadableStreamDefaultControllerEnqueue(stream.[[controller]], chunk).
        auto& default_controller = controller.get<JS::NonnullGCPtr<ReadableStreamDefaultController>>();
        return MUST(readable_stream_default_controller_enqueue(default_controller, chunk));
    }

    // 2. Otherwise,
    // 1. Assert: stream.[[controller]] implements ReadableByteStreamController.
    auto& byte_controller = controller.get<JS::NonnullGCPtr<ReadableByteStreamController>>();

    // 2. Assert: chunk is an ArrayBufferView.
    VERIFY(chunk.is_object());
    VERIFY(is<JS::TypedArrayBase>(chunk.as_object()) || is<JS::DataView>(chunk.as_object()));
    auto view = realm.heap().allocate<WebIDL::ArrayBufferView>(realm, chunk.as_object());

    // 3. Let byobView be the current BYOB request view for stream.
    auto byob_view = current_byob_request_view();

    // 4. If byobView is non-null, and chunk.[[ViewedArrayBuffer]] is byobView.[[ViewedArrayBuffer]], then:
    if (byob_view && view->viewed_array_buffer() == byob_view->viewed_array_buffer()) {
        // 1. Assert: chunk.[[ByteOffset]] is byobView.[[ByteOffset]].
        VERIFY(view->byte_offset() == byob_view->byte_offset());

        // 2. Assert: chunk.[[ByteLength]] ≤ byobView.[[ByteLength]].
        // Spec Note: These asserts ensure that the caller does not write outside the requested range in the current BYOB request view.
        VERIFY(view->byte_length() <= byob_view->byte_length());

        // 3. Perform ? ReadableByteStreamControllerRespond(stream.[[controller]], chunk.[[ByteLength]]).
        return readable_byte_stream_controller_respond(byte_controller, view->byte_length());
    }

    // 5. Otherwise, perform ? ReadableByteStreamControllerEnqueue(stream.[[controller]], chunk).
    return readable_byte_stream_controller_enqueue(byte_controller, chunk);
}

// https://streams.spec.whatwg.org/#readablestream-close
void ReadableStream::close()
{
    // 1. If stream.[[controller]] implements ReadableByteStreamController,
    VERIFY(m_controller.has_value());
    auto& controller = m_controller.value();
    if (controller.has<JS::NonnullGCPtr<ReadableByteStreamController>>()) {
        auto& byte_controller = controller.get<JS::NonnullGCPtr<ReadableByteStreamController>>();

        // 1. Perform ! ReadableByteStreamControllerClose(stream.[[controller]]).
        MUST(readable_byte_stream_controller_close(byte_controller));

        // 2. If stream.[[controller]].[[pendingPullIntos]] is not empty, perform ! ReadableByteStreamControllerRespond(stream.[[controller]], 0).
        if (!byte_controller->pending_pull_intos().is_empty())
            MUST(readable_byte_stream_controller_respond(byte_controller, 0));
    }
    // 2. Otherwise, perform ! ReadableStreamDefaultControllerClose(stream.[[controller]]).
    else {
        auto& default_controller = controller.get<JS::NonnullGCPtr<ReadableStreamDefaultController>>();
        readable_stream_default_controller_close(default_controller);
    }
}

// https://streams.spec.whatwg.org/#readablestream-current-byob-request-view
JS::GCPtr<WebIDL::ArrayBufferView> ReadableStream::current_byob_request_view()
{
    // 1. Assert: stream.[[controller]] implements ReadableByteStreamController.
    VERIFY(m_controller.has_value());
    auto& byte_controller = m_controller.value().get<JS::NonnullGCPtr<ReadableByteStreamController>>();

    // 2. Let byobRequest be ! ReadableByteStreamControllerGetBYOBRequest(stream.[[controller]]).
    auto byob_request = readable_byte_stream_controller_get_byob_request(byte_controller);

    // 3. If byobRequest is null, then return null.
    if (!byob_request)
        return nullptr;

    // 4. Return byobRequest.[[view]].
    return byob_request->view();
}

void ReadableStream::initialize(JS::Realm& realm)
{
    Base::initialize(realm);
    set_prototype(&Bindings::ensure_web_prototype<Bindings::ReadableStreamPrototype>(realm, "ReadableStream"_fly_string));
}

void ReadableStream::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    if (m_controller.has_value())
        m_controller->visit([&](auto& controller) { visitor.visit(controller); });
    visitor.visit(m_stored_error);
    if (m_reader.has_value())
        m_reader->visit([&](auto& reader) { visitor.visit(reader); });
}

// https://streams.spec.whatwg.org/#readablestream-locked
bool ReadableStream::is_readable() const
{
    // A ReadableStream stream is readable if stream.[[state]] is "readable".
    return m_state == State::Readable;
}

// https://streams.spec.whatwg.org/#readablestream-closed
bool ReadableStream::is_closed() const
{
    // A ReadableStream stream is closed if stream.[[state]] is "closed".
    return m_state == State::Closed;
}

// https://streams.spec.whatwg.org/#readablestream-errored
bool ReadableStream::is_errored() const
{
    // A ReadableStream stream is errored if stream.[[state]] is "errored".
    return m_state == State::Errored;
}
// https://streams.spec.whatwg.org/#readablestream-locked
bool ReadableStream::is_locked() const
{
    // A ReadableStream stream is locked if ! IsReadableStreamLocked(stream) returns true.
    return is_readable_stream_locked(*this);
}

// https://streams.spec.whatwg.org/#is-readable-stream-disturbed
bool ReadableStream::is_disturbed() const
{
    // A ReadableStream stream is disturbed if stream.[[disturbed]] is true.
    return m_disturbed;
}

}
