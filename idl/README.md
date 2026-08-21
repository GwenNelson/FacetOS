# Facet IDL

`.facet` files define one interface at a time for `facet-idlc`.

## Basic shape

```text
interface Name {
    uuid "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx";
    requires OtherInterface;
    method 1 ReturnType methodName(in u32 value, out u64 result);
    property 2 u32 count read write;
}
```

## Syntax

- A file contains a single `interface` declaration.
- The interface body contains any number of `uuid`, `requires`, `method`, and `property` entries.
- `//` comments and whitespace are ignored.

## Interface items

- `uuid "..."` sets the interface UUID. `IGenericObject` uses a fixed UUID.
- `requires OtherInterface;` declares another interface that this one depends on.
- `method <id> <ReturnType> <name>(...);` declares a method.
- `property <id> <Type> <name> read;`, `write;`, or `read write;` declares a property.

## Methods

- Method IDs are explicit numeric values.
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

## Properties

- Properties also use explicit numeric IDs.
- The generator emits them as fields in the generated interface struct.
- Access flags may be `read`, `write`, or both.

## Notes

- `facet-idlc` currently parses one interface per file.
- The generated C interface always starts with `self`, `priv`, and `getInterface`.
- Comments are line comments only; `/* ... */` is not supported.
