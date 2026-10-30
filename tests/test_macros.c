#include "macros.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const char *path = "test-shortcuts.txt";
    BsMacros macros = {0}, loaded = {0};
    strcpy(macros.text[0], "cq cq de iu2abc");
    memset(macros.text[8], 'A', BS_MAX_TEXT);
    assert(bs_macros_save(path, &macros));
    assert(bs_macros_load(path, &loaded));
    assert(!strcmp(loaded.text[0], "CQ CQ DE IU2ABC") && !*loaded.text[1]);
    assert(strlen(loaded.text[8]) == BS_MAX_TEXT);
    BsMacros original = loaded;
    const char *extra[] = {"macro1=OTHER", "macro0=NO", "macro10=NO", "unknown=NO", "schema=2"};
    for (size_t i = 0; i < sizeof extra / sizeof extra[0]; ++i) {
        assert(bs_macros_save(path, &macros));
        FILE *file = fopen(path, "a");
        assert(file && fprintf(file, "%s\n", extra[i]) > 0 && !fclose(file));
        assert(!bs_macros_load(path, &loaded) && !memcmp(&loaded, &original, sizeof loaded));
    }
    for (unsigned missing = 0; missing <= BS_MACRO_COUNT; ++missing) {
        FILE *file = fopen(path, "w");
        assert(file);
        if (missing != BS_MACRO_COUNT) {
            fputs("schema=1\n", file);
        }
        for (unsigned i = 0; i < BS_MACRO_COUNT; ++i) {
            if (i != missing) {
                fprintf(file, "macro%u=HELLO\n", i + 1);
            }
        }
        assert(!fclose(file));
        assert(!bs_macros_load(path, &loaded));
    }
    assert(bs_macros_save(path, &original));
    strcpy(macros.text[0], "BAD\nMACRO");
    assert(!bs_macros_save(path, &macros));
    assert(bs_macros_load(path, &loaded) && !memcmp(&loaded, &original, sizeof loaded));
    assert(!bs_macros_load("no-such-shortcuts.txt", &loaded));
    assert(!bs_macros_save("no-such-directory/shortcuts.txt", &original));
    char sibling[256];
    assert(bs_macros_path("/tmp/preferences/config.txt", sibling, sizeof sibling));
    assert(!strcmp(sibling, "/tmp/preferences/macros.txt"));
    assert(bs_macros_path("C:\\preferences\\config.txt", sibling, sizeof sibling));
    assert(!strcmp(sibling, "C:\\preferences\\macros.txt"));
    assert(bs_macros_path("config.txt", sibling, sizeof sibling) && !strcmp(sibling, "macros.txt"));
    assert(!bs_macros_path("", sibling, sizeof sibling));
    assert(!bs_macros_path("config.txt", sibling, 4));
    remove(path);
    puts("PASS macros: persistent slots, normalization, empty/max length, strict format, "
         "transactional failure and paths");
    return 0;
}
