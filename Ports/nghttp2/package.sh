#!/usr/bin/env -S bash ../.port_include.sh
port=nghttp2
version=1.47.0
files="https://github.com/nghttp2/nghttp2/releases/download/v${version}/nghttp2-${version}.tar.xz nghttp2-${version}.tar.xz 68271951324554c34501b85190f22f2221056db69f493afc3bbac8e7be21e7cc"
auth_type=sha256
useconfigure=true
depends=("openssl" "libev" "zlib" "c-ares" "libxml2" "jansson")
configopts=(
    "--disable-python-bindings"
)
