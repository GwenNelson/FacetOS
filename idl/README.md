# Facet IDL

`.facet` files define one interface at a time for `facet-idlc`.

## Basic shape

```text
interface Name {
    uuid "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
    requires OtherInterface;
    method ReturnType methodName(in u32 value, out u64 result);
    property u32 count read write;
}
```

## Syntax

- A file contains a single `interface` declaration.
- The interface body contains any number of `uuid`, `requires`, `method`, and `property` entries.
- `//` comments and whitespace are ignored.

Compile an interface with:

```sh
facet-idlc -o generated/IFoo.h idl/IFoo.facet
```

When compiling an interface other than `IGenericObject`, `facet-idlc` locates
and reads `IGenericObject.facet` automatically. Set
`FACET_IDL_GENERIC_OBJECT` when the generic IDL is not in the usual location.

## Interface items

- `uuid "..."` sets the interface UUID. `uuid auto;` generates fresh random
  bytes from the host entropy source and writes the result only into the
  generated header. It intentionally produces a new UUID on each compiler
  invocation; use an explicit UUID for an interface whose identity must persist.
- `requires OtherInterface;` declares another interface that this one depends on.
- `method <ReturnType> <name>(...);` declares a method. IDs come from
  declaration order.
- `property <Type> <name> read;`, `write;`, or `read write;` declares a
  property. Generated accessors participate in declaration ordering.

## Methods

- `IGenericObject.facet` defines `getInterface` first, so it receives method
  ID `0`. Its generated signature returns `FacetResult` and has an `out
  handle` result.
- `facet-idlc` reads `IGenericObject.facet` before compiling every other
  interface. Its declared methods are copied into the generated interface's
  method set at their declaration-order IDs in the reserved range 0--99. They are
  shared generic methods, not methods imported from `requires` declarations.
- Interface-specific methods begin at ID 100.
- Method and generated property-accessor IDs are ABI-visible. Reordering,
  inserting, or removing declarations changes later IDs; assign a new explicit
  interface UUID when changing an interface definition incompatibly.
- Parameters are written as `in`, `out`, or `inout` followed by a type and name.
- Supported type names include:
  - `u8`, `u16`, `u32`, `u64`
  - `i8`, `i16`, `i32`, `i64`
  - `bool`
  - `uuid`
  - `handle`
  - `string`
  - `local_ptr`
- Any other type name is treated as a raw generated C type name.

Object-returning methods use an ordinary `FacetResult` return value and an
`out handle` parameter. For example:

```text
method FacetResult getChild(out handle child);
```

## Properties

- Properties receive generated IDs through their accessors.
- The generator emits them as fields in the generated interface struct.
- Access flags may be `read`, `write`, or both.

## Notes

- `facet-idlc` currently parses one interface per file.
- The generated C interface always starts with `self` and `priv`, followed by
  the generated method set.
- Generated metadata includes the interface UUID, required-interface UUIDs,
  method IDs, parameter directions and parameter types.
- Comments are line comments only; `/* ... */` is not supported.
