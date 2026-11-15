ASSERT(_image_ram_end <= 0x20020000,
       "RAM use reaches 0x20020000, which supply_unused_ram_off() switches off (HANDOVER.md, section 9, unused RAM)")
