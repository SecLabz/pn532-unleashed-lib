# Minimal NFC-A reader

Initialize, read an unprotected NTAG213 once, and stop RF.
[Source to copy →](main.c) · [Build target `nfca_reader` →](../README.md)

The dump has static storage and is valid only after `NFC_OK`.
There is no USB output; inspect it in a debugger or add output after RF stops.
Successful reads keep selection; the explicit stop releases RF and checks cleanup.

Change the model passed to `nfca_select()` and the password passed to
`nfca_read_all()` for your tag.
The CMake transport choice sets the initial SPI/HSU configuration.
[NFC-A API →](../../docs/nfca.md)
