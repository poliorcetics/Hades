/******************************************************************************\
**
**  This file is part of the Hades GBA Emulator, and is made available under
**  the terms of the GNU General Public License version 2.
**
**  Copyright (C) 2020 - The Hades Authors
**
\******************************************************************************/

#include <string.h>
#include <errno.h>
#include "hades.h"
#include "core.h"
#include "debugger.h"
#include "rom.h"

int
main(
    int argc,
    char *argv[]
) {
    if (argc == 2) {
        struct core core;
        struct debugger debugger;
        FILE *file;

        file = fopen(argv[1], "rb");
        if (!file) {
            fprintf(stderr, "hades: can't open %s: %s", argv[1], strerror(errno));
            return (EXIT_FAILURE);
        }

        /* First, initialize the system and attach the debugger */

        core_init(
            &core,
            malloc(0x10000000),
            0x10000000
        );

        rom_load(&core, file);

        core_reset(&core);

        fclose(file);

        debugger_init(&debugger);
        debugger_attach(&debugger, &core);

        /* Then enter the debugger's REPL. */

        debugger_repl(&debugger);

        /* Finally, free all memory. */

        core_destroy(&core);

        return (EXIT_SUCCESS);
    } else {
        fprintf(stderr, "Usage: %s <path_to_rom>\n", argv[0]);
        return (EXIT_FAILURE);
    }
}
