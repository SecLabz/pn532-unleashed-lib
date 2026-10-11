/* The SDK retains the physical 2 MiB geometry. This application-only assertion
 * protects the receiver-profile sector and the following 384 KiB of tags. */
__rx_profile_start = 0x1019f000;
__tag_storage_start = 0x101a0000;
ASSERT(__flash_binary_end <= __rx_profile_start,
       "Firmware overlaps receiver configuration or reserved tag flash");
