ASSERT(_image_ram_end <= 0x20020000,
       "RAM use reaches 0x20020000, where the 60 Hz capture history lives (supply.c; HANDOVER.md, section 9, unused RAM)")
