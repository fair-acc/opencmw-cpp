
# Code formatting

Use `clang-format` for all your C++ formatting needs.
It is advisable to set up your editor to run `clang-format` on save,
or to define a Git hook that checks the formatting on commit.
Otherwise, the code will be rejected by the CI.


# C++ standard version

This project uses C++20 restricted to the features supported
by the latest stable versions of GCC and Clang.

Non-0mq-related parts of the project should also be compilable
to WebAssembly with Emscripten.


# External dependencies

The 3rd-party libraries are to be avoided, except in the following
cases:

- they are unavoidable (such as 0mq);
- they implement features that are planned for inclusion into
  a future C++ version in some form or another (such as refl-cpp and fmt);
- they are small enough to be incorporated into this project
  and a fork be developed and maintained inside of this project.


# Tests and examples

All code should be tested.

Apart from the tests, all library code needs to have
usage examples. Examples are located in the `concepts` directory.

It is fine to use TDD or example driven development
to drive the API design, but is not required as API
if most likely to go through several review revisions
before getting merged.


# Comments

Use comments when they would make the code more understandable.
Don't write useless comments.


# CamelCase versus snake_case

The C++ world is split into two camps with regards to the
preferred naming scheme for classes, variables and functions.

This project predominantly uses CamelCase. Classes start
with an uppercase letter (`DomainFilter`, `MdpMessage`) while
(member and non-member) variables and functions start
with a lowercase letter (`toString`, `tryLock`).

Exceptions to the CamelCase rule:

- named constants use all-uppercase snake_case style (`SELECTOR_PREFIX`);
- macros use all-uppercase snake_case style as well;
- namespaces are in all-lowercase snake_case style (`opencmw`, `opencmw::json`, `io_serialiser_yaml_test`);

- as the C++ Standard Library uses snake_case for everything,
  the code in this project that is meant to look like it belongs
  in the C++ Standard Library uses snake_case.

  This includes custom type traits defined in the project (akin to
  those in `<type_traits>`), generic algorithms that don't but could
  be a part of `<algorithm>`, etc.

  Template parameter names should still be CamelCase.


# Structures and classes

If no explicit encapsulation is needed,
prefer using `struct` to defining a "proper" class.

Private member variables should be prefixed with an underscore
and start with a lowercase letter (`_dimensions`, `_serviceCount`).
Member variables in structures and public member variables in classes
should not be prefixed with an underscore.

## Class internals order

Class and structure members should be defined in the following order
if possible:

1. Meta-information about the structure/class (`static_assert`s that
   check compile-time properties of the type such as checking template
   parameters are valid in class templates);
2. Member variables;
3. Public member functions;
4. Protected member functions;
5. Private member functions.

# Header-only code

Prefer writing header-only code (goes well hand-in-hand with the
guidelines on using `constexpr`).

While this slows down the compilation, it results in better code
generation.

# C++ features

## const

Use `const` whenever possible if doesn't hinder performance due to it disabling
the move semantics.

This includes:

 - local variables;
 - function arguments that are passed in as pointers or references;
 - member functions that don't modify the object
   (also add `[[nodiscard]]` to the return type of those member functions);
 - use `const_iterators` and `std::as_const` instead of normal iterators.

Exceptions:

 - don't declare member variables as `const` as it disables the move
   constructor and the move assignment operator;
 - don't declare normal variables as `const` if they could otherwise
   be `std::move`d to the function result or when passing to another function.

## constexpr

Use `constexpr` whenever possible.

## auto

Don't overuse `auto`. It is fine:

- when doing casts to avoid repeating the type name in the variable
  definition and in the cast specification;
- in generic code;
- for lambda parameters;
- when required (naming lambdas, structured bindings, etc.);
- when spelling out the type doesn't help understanding the code.

## Raw pointers

Raw pointers are fine if:

- they are required for interfacing with a C library 
  (prefer wrapping them into a RAII-enabled type);
- they are non-owning;
- they have a semantic of `optional<T&>` -- that is, they are used to denote
  a reference to an object that might not exist (if the pointer is used
  to point to something that can not be null, prefer using references
  and references to const)l
- if they are used without pointer arithmetic.

If you're using raw pointers outside of these parameters,
mark the code (comment) as unsafe. It is a good idea to encapsulate
code like this into a type that will hide the unsafety from its users.

## References vs values

Pass integral and floating point types as values. Pass everything else as references to const.
This applies also for lightweight types such as `std::string_view`.

Exceptions to the rule:

- sink arguments to constructors should be passed by value or in rare
  cases by forwarding references;
- code that immediately copes the reference-to-const argument
  to a local variable can potentially avoid that copy if reference-to-const
  was passed by value instead.

## Exceptions

Exceptions are enabled in this project.

## C++ Standard library algorithms

The C++ Standard Library exists for a reason.

Use `<algorithm>` whenever possible instead of writing raw loops.
Using any kind of loop should be a signal that your code does
something unorthodox and that the reader needs to pay more
attention when looking at it.

# static_assert and concepts

Use `static_asserts` to verify all compile-time assumptions,
concepts and `requires` for restricting function or class definitions
to specific types.

Prefer concepts to `static_assert` if the compile-time restriction
is expected to be checkable in other parts of the code (for example,
checking whether a function is callable with a specified argument type).


## Other modern C++ features

- Use enum classes instead of enums unless interfacing with a C library;
- use `override` and `final` (if virtual member functions are not replaceable
        with a compile-time dispatch alternative);
- never use `NULL` or `0` for pointers, use `nullptr` instead;


# C++ idioms

## std::variant and overloaded lambdas

When using `std::variant`, it is useful to define overloaded
lambdas for easier visitation:

https://www.cppstories.com/2019/02/2lines3featuresoverload.html/

## copy/move-and-swap assignment operators

In order to write correct assignment operators, use
the copy/move-and-swap idiom:

https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Copy-and-swap


# General coding practices

- Minimize variable scope. Don't define variables until they are used;
- avoid `using` and `using namespace`. When using `using` and `using namespace`,
  minimize the scope it affects;
- don't commit code that trigger compiler warnings;
- don't shadow variables in a local scope, give them new names;
- avoid `bool` function arguments in the API.

Consult [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines) when in doubt.


# Hints with using Emscripten

## Chromium

install https://goo.gle/wasm-debugging-extension

```shell
echo --auto-open-devtools-for-tabs >> ~/config/chromium-flags.conf
```



