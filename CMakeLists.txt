cmake_minimum_required(VERSION 3.9)

project( sqnice
    VERSION     0.1.0
    DESCRIPTION "A nice C++ API for SQLite"
    LANGUAGES   CXX C
)


set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)


option(USE_LOCAL_SQLITE    "Use copy of sqlite in the vendor subdirectory" OFF)
option(USE_LOCAL_SQLCIPHER "Use copy of SQLCipher in the vendor subdirectory" OFF)


#### CONFIG


if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
    set(USE_LOCAL_SQLITE ON) #FIXME: Don't override the option
else()
    # Clang & GCC:
    add_compile_options(
        -Werror
        -Wall
        -Wpedantic
        -Wno-unknown-pragmas
    )
    if (CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_GNUCXX)
        # GCC-specific:
        add_compile_options(
            -Wno-psabi          # suppress annoying GCC ABI warning
            -Wno-sign-compare   # apparently comes with `pedantic` in GCC
            -D_FORTIFY_SOURCE=2 # static+dynamic buffer-overflow checks
        )
    else()
        # Clang-specific:
        add_compile_options(
            -Wno-nullability-extension
            -Wno-gnu-zero-variadic-macro-arguments
            -Wno-gnu-statement-expression-from-macro-expansion
            -Wno-ambiguous-reversed-operator
        )
    endif()
endif()


if (USE_LOCAL_SQLCIPHER)
    include_directories( BEFORE SYSTEM
        vendor/sqlcipher/
    )
elseif (USE_LOCAL_SQLITE)
    include_directories( BEFORE SYSTEM
        vendor/sqlite/
    )
endif()

add_library( sqnice STATIC
    src/base.cc
    src/blob_stream.cc
    src/database.cc
    src/functions.cc
    src/pool.cc
    src/query.cc
    src/transaction.cc
)

target_include_directories( sqnice PUBLIC
    include/
)

if (USE_LOCAL_SQLCIPHER)
    target_sources( sqnice PRIVATE
        vendor/sqlcipher/sqlite3.c
    )
    set_source_files_properties(
        vendor/sqlcipher/sqlite3.c  PROPERTIES COMPILE_OPTIONS "-Wno-error"
    )
    target_compile_definitions(sqnice PRIVATE
        SQLITE_HAS_CODEC
        SQLITE_TEMP_STORE=3
    )
    if(APPLE)
        target_compile_definitions(sqnice PRIVATE
            SQLCIPHER_CRYPTO_CC     # Use Apple's CommonCrypto lib for encryption
        )
        target_link_libraries( sqnice INTERFACE
            "-framework CoreFoundation"
            "-framework Security"
        )
    else()
        target_compile_definitions(sqnice PRIVATE
            SQLCIPHER_CRYPTO_OPENSSL
        )
        target_link_libraries( sqnice INTERFACE
            crypto
        )
    endif()
elseif (USE_LOCAL_SQLITE)
    target_sources( sqnice PRIVATE
        vendor/sqlite/sqlite3.c
    )
else()
    target_link_libraries( sqnice INTERFACE
        sqlite3
    )
endif()


#### TESTS


add_executable( sqnice_tests
    test/testdb.cc
    test/testfunctions.cc
    test/testquery.cc
    test/test_main.cc
)

target_link_libraries( sqnice_tests
    sqnice
)

