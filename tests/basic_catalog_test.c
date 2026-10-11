#include "basic_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect(int condition, const char *message)
{
    if (!condition)
    {
        printf("FAIL: %s\n", message);
        ++failures;
    }
}

static int parse_text(const char *json, basic_catalog_t *catalog)
{
    return basic_catalog_parse(json, strlen(json), catalog);
}

static char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    char *buffer;
    long size;
    if (file == NULL) return NULL;
    if ((fseek(file, 0, SEEK_END) != 0) || ((size = ftell(file)) < 0) ||
        (fseek(file, 0, SEEK_SET) != 0))
    {
        fclose(file);
        return NULL;
    }
    buffer = malloc((size_t)size + 1U);
    if ((buffer != NULL) && (fread(buffer, 1U, (size_t)size, file) != (size_t)size))
    {
        free(buffer);
        buffer = NULL;
    }
    fclose(file);
    if (buffer != NULL)
    {
        buffer[size] = '\0';
        *length = (size_t)size;
    }
    return buffer;
}

int main(int argc, char **argv)
{
    static basic_catalog_t catalog;
    static basic_catalog_t unchanged;
    size_t length = 0U;
    char *json;
    char overlong[400];

    if (argc != 2)
    {
        printf("usage: basic_catalog_test progs.json\n");
        return 2;
    }
    json = read_file(argv[1], &length);
    expect(json != NULL, "read catalog fixture");
    if (json != NULL)
    {
        expect(basic_catalog_parse(json, length, &catalog), "parse fixture");
        expect(catalog.count == 2U, "fixture count");
        expect(strcmp(catalog.programs[0].filename, "trek.bas") == 0, "trek filename");
        expect(strcmp(catalog.programs[0].type, "game") == 0, "trek type");
        expect(catalog.programs[0].size == 28406U, "trek size");
        expect(strcmp(catalog.programs[1].filename, "amazing.bas") == 0, "amazing filename");
        expect(catalog.programs[1].size == 3094U, "amazing size");
        expect(basic_catalog_find(&catalog, "TREK.BAS") == &catalog.programs[0], "find exact");
        expect(basic_catalog_find(&catalog, "Amazing") == &catalog.programs[1], "find stem");
        expect(basic_catalog_find(&catalog, "trek.ba") == NULL, "reject partial");
        expect(basic_catalog_find(&catalog, "missing") == NULL, "reject missing");
        free(json);
    }

    expect(parse_text("{\"x\":[1,-2.5e+3,true,false,null,{\"a\":[]}],\"programs\":["
                      "{\"filename\":\"a-b_1.bas\",\"size\":0,\"description\":\"A\\u0041\\/\\\"\","
                      "\"extra\":{\"n\":[1]}}]}", &catalog), "parse escapes and unknown values");
    expect(strcmp(catalog.programs[0].description, "AA/\"") == 0, "decode escapes");
    expect(parse_text("{\"programs\":[]}", &catalog) && (catalog.count == 0U), "empty catalog");

    unchanged = catalog;
    const char *invalid[] = {
        "",
        "{}",
        "{\"programs\":{}}",
        "{\"programs\":[{\"size\":1}]}",
        "{\"programs\":[{\"filename\":\"a.bas\"}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1},{\"filename\":\"A.BAS\",\"size\":2}]}",
        "{\"programs\":[{\"filename\":\"../x.bas\",\"size\":1}]}",
        "{\"programs\":[{\"filename\":\"/x.bas\",\"size\":1}]}",
        "{\"programs\":[{\"filename\":\".x.bas\",\"size\":1}]}",
        "{\"programs\":[{\"filename\":\"x?y.bas\",\"size\":1}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":4294967296}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":-1}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1.5}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":01}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1,\"type\":\"\\q\"}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1,\"type\":\"\\u00e9\"}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1,\"type\":\"a\nb\"}]}",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1}]} x",
        "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1,}]}",
        "{\"x\":[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]],\"programs\":[]}",
    };
    for (size_t index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
    {
        if (parse_text(invalid[index], &catalog))
        {
            printf("FAIL: accepted invalid catalog %u\n", (unsigned int)index);
            ++failures;
        }
    }
    expect(memcmp(&catalog, &unchanged, sizeof(catalog)) == 0, "failed parse leaves output");

    const char *prefix = "{\"programs\":[{\"filename\":\"a.bas\",\"size\":1,\"description\":\"";
    memset(overlong, 'x', sizeof(overlong));
    memcpy(overlong, prefix, strlen(prefix));
    strcpy(&overlong[sizeof(overlong) - 5U], "\"}]}");
    expect(!parse_text(overlong, &catalog), "reject overlong description");

    if (failures == 0)
    {
        printf("Catalog tests passed.\n");
    }
    return failures == 0 ? 0 : 1;
}
