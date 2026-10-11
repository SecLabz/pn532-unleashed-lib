# Synthetic image fixtures

Each JSON file contains hex-encoded sections and a complete PNU1 image for
one supported model. These are frozen test inputs, with no private dumps or
signing material. Tests read their sections/binary bytes and call production
`image_validate()` or `image_parse()`; no second parser is needed.

The fixtures were originally validated and serialized by a Rust image
serializer. On an intentional format change, update the sections and complete
binary together, check them with the C validator, then run
`./tests/run_host.sh` from the library root.
[Image layout →](../../../docs/images.md)
