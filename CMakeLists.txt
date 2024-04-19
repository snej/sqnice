cmake_minimum_required(VERSION 3.9)

project( sqnice
    VERSION     0.1.0
    DESCRIPTION "A C++ API for SQLite"
    LANGUAGES   CXX
)


set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)


#### CONFIG


if (MSVC)
    # MSVC:
    add_definitions(-DWIN32_LEAN_AND_MEAN -D_WIN32_WINNT=0x0A00 -DNOMINMAX)
else()
    # Clang & GCC:
    add_compile_options(
        -Werror
        -Wall
        -Wpedantic
        -Wno-unknown-pragmas
        -Wno-unknown-warning-option
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
            -Wno-gnu-zero-variadic-macro-arguments
            -Wno-gnu-statement-expression-from-macro-expansion
            -Wno-ambiguous-reversed-operator
        )
    endif()
endif()


add_library( sqnice STATIC
    src/sqnice.cpp
    src/sqniceext.cpp
)

target_include_directories( sqnice PUBLIC
    src/
)

target_compile_options( sqnice PRIVATE
    -Wno-deprecated-declarations
)


#### TESTS


add_executable( sqnice_tests
    test/testaggregate.cpp
    test/testattach.cpp
    test/testbackup.cpp
    test/testcallback.cpp
    test/testdb.cpp
    test/testdisconnect.cpp
    test/testfunction.cpp
    test/testinsert.cpp
    test/testinsertall.cpp
    test/testselect.cpp
)

target_compile_options( sqnice_tests PRIVATE
    -Wno-deprecated-declarations
)

target_link_libraries( sqnice_tests
    sqnice
    sqlite3
)

