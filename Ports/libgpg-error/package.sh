#!/usr/bin/env -S bash ../.port_include.sh
port=libgpg-error
version=1.45
useconfigure=true
use_fresh_config_sub=true
config_sub_path=build-aux/config.sub
depends=("gettext")
configopts=("--disable-tests" "--disable-threads")
files="https://gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-${version}.tar.bz2 libgpg-error-${version}.tar.bz2 570f8ee4fb4bff7b7495cff920c275002aea2147e9a1d220c068213267f80a26"
auth_type=sha256

configure() {
    run ./configure --host="${SERENITY_ARCH}-pc-serenity" --build="$($workdir/build-aux/config.guess)" "${configopts[@]}"
}

install() {
    run make DESTDIR=${SERENITY_INSTALL_ROOT} "${installopts[@]}" install
    ${CC} -shared -o ${SERENITY_INSTALL_ROOT}/usr/local/lib/libgpg-error.so -Wl,-soname,libgpg-error.so -Wl,--whole-archive ${SERENITY_INSTALL_ROOT}/usr/local/lib/libgpg-error.a -Wl,--no-whole-archive -lintl
    rm -f ${SERENITY_INSTALL_ROOT}/usr/local/lib/libgpg-error.la
}
