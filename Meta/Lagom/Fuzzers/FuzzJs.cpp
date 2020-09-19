/*
 * Copyright (c) 2020, the SerenityOS developers.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <AK/String.h>
#include <AK/StringView.h>
#include <LibJS/Lexer.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <signal.h>

#include <stddef.h>
#include <stdint.h>

static const char* data;
static size_t size;

class TestRunnerGlobalObject : public JS::GlobalObject {
public:
    TestRunnerGlobalObject();
    virtual ~TestRunnerGlobalObject() override;

    virtual void initialize() override;

private:
    virtual const char* class_name() const override { return "TestRunnerGlobalObject"; }
};

TestRunnerGlobalObject::TestRunnerGlobalObject()
{
}

TestRunnerGlobalObject::~TestRunnerGlobalObject()
{
}

void TestRunnerGlobalObject::initialize()
{
    JS::GlobalObject::initialize();
    define_property("global", this, JS::Attribute::Enumerable);
}

static void segfault_handler(int, siginfo_t*, void*) {
    dbg() << "Segmentation fault!";
    for (size_t i = 0; i < size; ++i) {
        dbg() << String::format("%02x", data[size]) << " ";
    }
    ASSERT_NOT_REACHED();
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    struct sigaction sa;

    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_handler;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        ASSERT_NOT_REACHED();
    }

    auto js = AK::StringView(static_cast<const unsigned char*>(data), size);
    auto lexer = JS::Lexer(js);
    auto parser = JS::Parser(lexer);
    auto program = parser.parse_program();
    if (parser.has_errors())
        return 1;

    auto interpreter = JS::Interpreter::create<TestRunnerGlobalObject>();
    interpreter->run(interpreter->global_object(), *program);
    return 0;
}
